"""中文：主机离线出题、逻辑分级、证明回放与紧凑题库导出。
English: host-only generation, logical grading, proof replay and compact bank export.
"""
from pathlib import Path
from collections import Counter
from itertools import combinations
import argparse
import csv
import hashlib
import json
import random
import struct
import time
import zlib
from concurrent.futures import ProcessPoolExecutor

ROOT = Path(__file__).resolve().parent
ROWS = [tuple(r*9+c for c in range(9)) for r in range(9)]
COLS = [tuple(r*9+c for r in range(9)) for c in range(9)]
BOXES = [tuple((br*3+r)*9+bc*3+c for r in range(3) for c in range(3)) for br in range(3) for bc in range(3)]
UNITS = ROWS + COLS + BOXES
CELL_UNITS = [tuple(u for u in UNITS if i in u) for i in range(81)]
PEERS = [set().union(*CELL_UNITS[i]) - {i} for i in range(81)]
DIGITS = set(range(1, 10))
TIERS = ['easy', 'medium', 'hard', 'expert']
RANK = {'naked_single': 0, 'hidden_single': 0, 'locked_candidate': 1,
        'naked_pair': 2, 'hidden_pair': 2, 'naked_triple': 3, 'x_wing': 3}


def parse(grid):
    if len(grid) != 81 or any(c not in '0123456789' for c in grid):
        raise ValueError('grid must contain exactly 81 digits')
    return [int(c) for c in grid]


def count_solutions(grid, rng=None):
    board = parse(grid)
    rows, cols, boxes = [0]*9, [0]*9, [0]*9
    for i, n in enumerate(board):
        if not n:
            continue
        r, c, b = i//9, i%9, (i//27)*3 + (i%9)//3
        bit = 1 << (n-1)
        if (rows[r] | cols[c] | boxes[b]) & bit:
            return 0, None
        rows[r] |= bit
        cols[c] |= bit
        boxes[b] |= bit
    found = []
    limit = 1 if rng is not None else 2

    def search():
        chosen, mask, best = -1, 0, 10
        for i, n in enumerate(board):
            if n:
                continue
            r, c, b = i//9, i%9, (i//27)*3 + (i%9)//3
            available = 511 & ~(rows[r] | cols[c] | boxes[b])
            size = available.bit_count()
            if size == 0:
                return
            if size < best:
                chosen, mask, best = i, available, size
                if size == 1:
                    break
        if chosen < 0:
            found.append(''.join(map(str, board)))
            return
        r, c, b = chosen//9, chosen%9, (chosen//27)*3 + (chosen%9)//3
        options = [1 << k for k in range(9) if mask & (1 << k)]
        if rng:
            rng.shuffle(options)
        for bit in options:
            board[chosen] = bit.bit_length()
            rows[r] |= bit
            cols[c] |= bit
            boxes[b] |= bit
            search()
            rows[r] ^= bit
            cols[c] ^= bit
            boxes[b] ^= bit
            board[chosen] = 0
            if len(found) >= limit:
                return
    search()
    return len(found), found[0] if found else None


def elimination(technique, pattern, removals, **extra):
    return {'technique': technique, 'pattern': list(pattern), 'removals': removals, **extra}


def find_locked(candidates):
    for ui, unit in enumerate(UNITS):
        for digit in range(1, 10):
            positions = [i for i in unit if digit in candidates.get(i, ())]
            if len(positions) < 2:
                continue
            for vi, other in enumerate(UNITS):
                if (ui < 18) == (vi < 18):
                    continue
                if not set(positions).issubset(other):
                    continue
                removals = [{'cell': i, 'digits': [digit]} for i in other
                            if i not in unit and digit in candidates.get(i, ())]
                if removals:
                    return elimination('locked_candidate', positions, removals,
                                       unit=ui, other_unit=vi, digit=digit)


def find_subset(candidates, size):
    for ui, unit in enumerate(UNITS):
        eligible = [i for i in unit if 2 <= len(candidates.get(i, ())) <= size]
        for group in combinations(eligible, size):
            values = set().union(*(candidates[i] for i in group))
            if len(values) != size:
                continue
            removals = [{'cell': i, 'digits': sorted(candidates[i] & values)}
                        for i in unit if i in candidates and i not in group and candidates[i] & values]
            if removals:
                return elimination('naked_pair' if size == 2 else 'naked_triple', group, removals,
                                   unit=ui, digits=sorted(values))


def find_naked_pair(candidates):
    return find_subset(candidates, 2)


def find_hidden_pair(candidates):
    for ui, unit in enumerate(UNITS):
        for digits in combinations(range(1, 10), 2):
            positions = [{i for i in unit if d in candidates.get(i, ())} for d in digits]
            if len(positions[0]) != 2 or positions[0] != positions[1]:
                continue
            group = sorted(positions[0])
            removals = [{'cell': i, 'digits': sorted(candidates[i] - set(digits))}
                        for i in group if candidates[i] - set(digits)]
            if removals:
                return elimination('hidden_pair', group, removals, unit=ui, digits=list(digits))


def find_xwing(candidates):
    for axis in ('row', 'column'):
        units = ROWS if axis == 'row' else COLS
        cross = (lambda i: i % 9) if axis == 'row' else (lambda i: i // 9)
        for digit in range(1, 10):
            pairs = [(u, tuple(i for i in unit if digit in candidates.get(i, ())))
                     for u, unit in enumerate(units)]
            pairs = [(u, p) for u, p in pairs if len(p) == 2]
            for (a, pa), (b, pb) in combinations(pairs, 2):
                positions = {cross(i) for i in pa}
                if positions != {cross(i) for i in pb}:
                    continue
                pattern = list(pa + pb)
                removals = [{'cell': i, 'digits': [digit]} for i in sorted(candidates)
                            if i not in pattern and cross(i) in positions and digit in candidates[i]]
                if removals:
                    return elimination('x_wing', pattern, removals, axis=axis,
                                       units=[a, b], digit=digit)


def initial_candidates(board):
    return {i: DIGITS - {board[p] for p in PEERS[i]} for i in range(81) if not board[i]}


def logical_solve(grid):
    board = parse(grid)
    for unit in UNITS:
        placed = [board[i] for i in unit if board[i]]
        if len(placed) != len(set(placed)):
            return {'solution': grid, 'trace': [], 'difficulty': None}
    candidates = initial_candidates(board)
    trace = []
    while candidates:
        step = None
        if any(not values for values in candidates.values()):
            break
        for i, values in candidates.items():
            if len(values) == 1:
                step = {'technique': 'naked_single', 'cell': i, 'digit': next(iter(values))}
                break
        if step is None:
            for ui, unit in enumerate(UNITS):
                for n in range(1, 10):
                    positions = [i for i in unit if n in candidates.get(i, ())]
                    if len(positions) == 1:
                        step = {'technique': 'hidden_single', 'cell': positions[0], 'digit': n, 'unit': ui}
                        break
                if step:
                    break
        if step is None:
            for tactic in (find_locked, find_naked_pair, find_hidden_pair,
                           lambda c: find_subset(c, 3), find_xwing):
                step = tactic(candidates)
                if step:
                    break
        if step is None:
            break
        trace.append(step)
        apply_step(board, candidates, step)
    solved = not candidates
    return {'solution': ''.join(map(str, board)), 'trace': trace,
            'difficulty': TIERS[max((RANK[s['technique']] for s in trace), default=0)] if solved else None}


def apply_step(board, candidates, step):
    if 'cell' in step:
        i, digit = step['cell'], step['digit']
        board[i] = digit
        del candidates[i]
        for p in PEERS[i]:
            if p in candidates:
                candidates[p].discard(digit)
    else:
        for change in step['removals']:
            candidates[change['cell']].difference_update(change['digits'])


def verify_trace(grid, trace, solution):
    """中文：按规则前提独立回放证明，不调用技巧搜索器。
    English: replay rule premises independently without calling technique finders.
    """
    board = parse(grid)
    candidates = initial_candidates(board)
    for step in trace:
        tech = step['technique']
        if tech in ('naked_single', 'hidden_single'):
            i, n = step['cell'], step['digit']
            assert n in candidates[i] and n == int(solution[i])
            if tech == 'naked_single':
                assert candidates[i] == {n}
            else:
                assert [p for p in UNITS[step['unit']] if n in candidates.get(p, ())] == [i]
        else:
            group = set(step['pattern'])
            if tech in ('naked_pair', 'naked_triple'):
                k = 2 if tech == 'naked_pair' else 3
                unit = set(UNITS[step['unit']])
                values = set(step['digits'])
                assert len(group) == len(values) == k and group <= unit
                assert set().union(*(candidates[p] for p in group)) == values
                allowed = {p: candidates[p] & values for p in unit-group if p in candidates}
            elif tech == 'hidden_pair':
                unit = set(UNITS[step['unit']])
                values = set(step['digits'])
                assert len(group) == len(values) == 2 and group <= unit
                for n in values:
                    assert {p for p in unit if n in candidates.get(p, ())} == group
                allowed = {p: candidates[p] - values for p in group}
            elif tech == 'locked_candidate':
                unit, other = set(UNITS[step['unit']]), set(UNITS[step['other_unit']])
                n = step['digit']
                assert (step['unit'] < 18) != (step['other_unit'] < 18)
                assert {p for p in unit if n in candidates.get(p, ())} == group
                assert len(group) >= 2 and group <= other
                allowed = {p: {n} & candidates[p] for p in other-unit if p in candidates}
            elif tech == 'x_wing':
                n = step['digit']
                units = ROWS if step['axis'] == 'row' else COLS
                cross = (lambda p: p % 9) if step['axis'] == 'row' else (lambda p: p // 9)
                a, b = step['units']
                assert a != b
                pa = {p for p in units[a] if n in candidates.get(p, ())}
                pb = {p for p in units[b] if n in candidates.get(p, ())}
                assert len(pa) == len(pb) == 2 and pa | pb == group
                assert {cross(p) for p in pa} == {cross(p) for p in pb}
                allowed = {p: {n} & candidates[p] for p in candidates if p not in group and cross(p) in {cross(q) for q in pa}}
            else:
                raise AssertionError(tech)
            assert step['removals']
            for change in step['removals']:
                p, values = change['cell'], set(change['digits'])
                assert values and values <= allowed.get(p, set())
                assert int(solution[p]) not in values
        apply_step(board, candidates, step)
        assert all(candidates.values())
    assert not candidates and ''.join(map(str, board)) == solution
    return True


def canonical(grid):
    variants = []
    matrix = [list(grid[r*9:r*9+9]) for r in range(9)]
    for _ in range(4):
        for m in (matrix, [list(reversed(row)) for row in matrix]):
            mapping = {'0': '0'}
            out = []
            for row in m:
                for n in row:
                    if n not in mapping:
                        mapping[n] = str(len(mapping))
                    out.append(mapping[n])
            variants.append(''.join(out))
        matrix = [list(row) for row in zip(*matrix[::-1])]
    return min(variants)


def make_puzzle(rng):
    _, solution = count_solutions('0'*81, rng)
    grid = list(solution)
    order = list(range(81))
    rng.shuffle(order)
    target = rng.choice([23, 24, 25, 26, 27, 28, 30, 33, 36])
    remaining = 81
    for i in order:
        old, grid[i] = grid[i], '0'
        if count_solutions(''.join(grid))[0] != 1:
            grid[i] = old
        else:
            remaining -= 1
        if remaining <= target:
            break
    return ''.join(grid), solution


def candidate_batch(task):
    seed, batch = task
    rng = random.Random((seed << 32) + batch)
    result = []
    for _ in range(32):
        grid, solution = make_puzzle(rng)
        result.append((grid, solution, logical_solve(grid)))
    return result


def candidate_stream(seed, workers):
    # 中文：仅主机多进程计算，绝不访问设备或SDK；批次数量有界。
    # English: bounded host worker batches never access the device or SDK.
    with ProcessPoolExecutor(max_workers=workers) as pool:
        batch = 0
        while True:
            tasks = [(seed, batch+i) for i in range(workers)]
            for results in pool.map(candidate_batch, tasks):
                yield from results
            batch += workers


def generate(per_tier, seed, workers=8):
    output = ROOT/'bank'
    output.mkdir(exist_ok=True)
    checkpoint = output/'checkpoint.jsonl'
    records = []
    seen = set()
    counts = Counter()
    prior_records = []
    if checkpoint.exists():
        prior_records = [json.loads(line) for line in checkpoint.read_text(encoding='utf-8').splitlines()]
    elif (output/'puzzles.json').exists():
        prior_records = json.loads((output/'puzzles.json').read_text(encoding='utf-8'))['records']
    if prior_records:
        for record in prior_records:
            if record['seed'] != seed:
                raise ValueError('checkpoint seed differs; use a separate output directory')
            if counts[record['difficulty']] < per_tier:
                records.append(record)
                seen.add(canonical(record['grid']))
                counts[record['difficulty']] += 1
    stream = candidate_stream(seed, workers)
    attempts, start = 0, time.monotonic()
    with checkpoint.open('a', encoding='utf-8') as checkpoint_file:
        while any(counts[t] < per_tier for t in TIERS):
            attempts += 1
            grid, solution, result = next(stream)
            key = canonical(grid)
            if key in seen:
                continue
            tier = result['difficulty']
            if tier is not None and counts[tier] < per_tier:
                assert result['solution'] == solution
                verify_trace(grid, result['trace'], solution)
                record = {'seed': seed, 'difficulty': tier, 'grid': grid, 'solution': solution,
                          'clues': 81-grid.count('0'), 'techniques': dict(Counter(s['technique'] for s in result['trace'])),
                          'trace': result['trace'], 'canonical_sha256': hashlib.sha256(key.encode()).hexdigest()}
                records.append(record)
                counts[tier] += 1
                seen.add(key)
                checkpoint_file.write(json.dumps(record, separators=(',', ':'))+'\n')
                checkpoint_file.flush()
            if attempts % 1000 == 0:
                print(json.dumps({'attempts': attempts, 'accepted': dict(counts),
                                  'elapsed_s': round(time.monotonic()-start, 1)}), flush=True)
    stream.close()
    records.sort(key=lambda r: (TIERS.index(r['difficulty']), r['canonical_sha256']))
    for record in records:
        record['id'] = f"PN-SDK-1-{record['canonical_sha256'][:8]}"
    assert len({r['id'] for r in records}) == len(records), 'record-id collision requires a new bank schema'
    (output/'puzzles.json').write_text(json.dumps({'schema': 1, 'seed': seed, 'per_tier': per_tier,
        'rating': 'PicoNano logical profile v1, not a universal rating', 'records': records}, indent=2), encoding='utf-8')
    with (output/'puzzles.csv').open('w', newline='', encoding='utf-8-sig') as f:
        writer = csv.DictWriter(f, fieldnames=['id', 'difficulty', 'clues', 'grid', 'solution'])
        writer.writeheader()
        writer.writerows({k: r[k] for k in writer.fieldnames} for r in records)
    body = bytearray()
    for i, r in enumerate(records):
        digits = [int(c) for c in r['grid']] + [0]
        packed = bytes((digits[j] << 4) | digits[j+1] for j in range(0, 82, 2))
        body.extend(packed + struct.pack('<BBBI', TIERS.index(r['difficulty']), r['clues'], 0,
                                       int(r['canonical_sha256'][:8], 16)))
    header = struct.pack('<4sHHIIIIII', b'PNSD', 1, 48, len(records), seed, zlib.crc32(body), 0, 0, 0)
    (output/'puzzles.pnsd').write_bytes(header+body)
    summary = {'schema': 1, 'seed': seed, 'counts': dict(Counter(r['difficulty'] for r in records)),
               'total': len(records), 'record_bytes': 48, 'header_bytes': 32,
               'device_bank_bytes': len(header+body), 'max_logical_steps': max(len(r['trace']) for r in records),
               'clue_ranges': {t: [min(r['clues'] for r in records if r['difficulty']==t),
                                   max(r['clues'] for r in records if r['difficulty']==t)] for t in TIERS},
               'binary_sha256': hashlib.sha256(header+body).hexdigest()}
    (output/'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    print(json.dumps(summary), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--per-tier', type=int, default=300)
    parser.add_argument('--seed', type=int, default=20261024)
    parser.add_argument('--workers', type=int, default=8)
    args = parser.parse_args()
    if args.per_tier < 1 or args.workers < 1:
        parser.error('--per-tier and --workers must be positive')
    generate(args.per_tier, args.seed, args.workers)
