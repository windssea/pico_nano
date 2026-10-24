"""中文：独立精确覆盖验解，并核验导出尺寸、灰阶和二进制。
English: independent exact-cover uniqueness audit and export geometry/palette/binary checks.
"""
from pathlib import Path
from collections import Counter
import hashlib
import json
import struct
import zlib
from PIL import Image
import sudoku_bank as bank

ROOT = Path(__file__).resolve().parent


def exact_cover_solutions(grid):
    # 中文：使用324列精确覆盖，与出题器的位掩码回溯分开实现。
    # English: 324-column exact cover is separate from the generator's bitmask backtracking.
    choices = {}
    columns = {i: set() for i in range(324)}
    for r in range(9):
        for c in range(9):
            digits = [int(grid[r*9+c])] if grid[r*9+c] != '0' else range(1, 10)
            for n in digits:
                row = (r, c, n)
                constraints = (r*9+c, 81+r*9+n-1, 162+c*9+n-1,
                               243+((r//3)*3+c//3)*9+n-1)
                choices[row] = constraints
                for col in constraints:
                    columns[col].add(row)
    results = []

    def search(selected):
        if not columns:
            output = ['0']*81
            for r, c, n in selected:
                output[r*9+c] = str(n)
            results.append(''.join(output))
            return
        col = min(columns, key=lambda c: len(columns[c]))
        for row in tuple(columns[col]):
            removed = []
            for j in choices[row]:
                for other in columns[j]:
                    for k in choices[other]:
                        if k != j:
                            columns[k].remove(other)
                removed.append((j, columns.pop(j)))
            search(selected+[row])
            for j, values in reversed(removed):
                columns[j] = values
                for other in values:
                    for k in choices[other]:
                        if k != j:
                            columns[k].add(other)
            if len(results) >= 2:
                break
    search([])
    return results


def main():
    manifest = json.loads((ROOT/'manifest.json').read_text(encoding='utf-8'))
    png_count = 0
    for entry in manifest['assets']:
        path = ROOT/entry['file']
        im = Image.open(path)
        assert im.size == (entry['width'], entry['height']), path
        assert hashlib.sha256(path.read_bytes()).hexdigest() == entry['sha256'], path
        rgba = im.convert('RGBA')
        colors = set(rgba.getdata())
        assert all(r == g == b and r % 17 == 0 and a in (0, 255) for r, g, b, a in colors), path
        if entry['file'].startswith('screens/'):
            assert im.size == (684, 1216)
        if im.mode == 'P':
            header = path.read_bytes()
            assert header[24] == 4 and header[25] == 3, path
        png_count += 1
    for size in (96, 144):
        boxes = [Image.open(ROOT/f'assets/panda_{name}_{size}.png').getchannel('A').getbbox() for name in bank_names]
        assert len({b[3] for b in boxes}) == 1, boxes
        assert all(b[0] > 0 and b[1] > 0 and b[2] < size and b[3] < size for b in boxes)
    layout = json.loads((ROOT/'layout.json').read_text(encoding='utf-8'))
    assert len(layout['difficulty_rows']) == 4
    for group in ('number_keys', 'tools', 'difficulty_rows'):
        for item in layout[group]:
            x, y, w, h = item['rect']
            assert min(x, y) >= 0 and x+w <= 684 and y+h <= 1216
    document = json.loads((ROOT/'bank/puzzles.json').read_text(encoding='utf-8'))
    records = document['records']
    assert Counter(r['difficulty'] for r in records) == {t: document['per_tier'] for t in bank.TIERS}
    canonical = set()
    trace_count = 0
    techniques = Counter()
    for index, record in enumerate(records):
        grid, solution = record['grid'], record['solution']
        assert exact_cover_solutions(grid) == [solution], record['id']
        for unit in bank.UNITS:
            assert {solution[p] for p in unit} == set('123456789')
        assert all(a == '0' or a == b for a, b in zip(grid, solution))
        key = bank.canonical(grid)
        assert key not in canonical
        canonical.add(key)
        assert hashlib.sha256(key.encode()).hexdigest() == record['canonical_sha256']
        assert bank.verify_trace(grid, record['trace'], solution)
        rerated = bank.logical_solve(grid)
        assert bank.product_tier(grid, rerated) == record['difficulty']
        assert rerated['solution'] == solution
        trace_count += len(record['trace'])
        techniques.update(step['technique'] for step in record['trace'])
        if (index+1) % 300 == 0:
            print(f'Independently verified {index+1}/{len(records)} puzzles', flush=True)
    payload = (ROOT/'bank/puzzles.pnsd').read_bytes()
    magic, version, size, count, seed, crc, a, b, c = struct.unpack('<4sHHIIIIII', payload[:32])
    assert (magic, version, size, count, a, b, c) == (b'PNSD', 1, 48, len(records), 0, 0, 0)
    assert len(payload) == 32+count*size and zlib.crc32(payload[32:]) == crc
    for i, record in enumerate(records):
        data = payload[32+i*48:32+(i+1)*48]
        unpacked = ''.join(str(n) for byte in data[:41] for n in (byte >> 4, byte & 15))
        assert unpacked[:81] == record['grid'] and unpacked[81] == '0'
        difficulty, clues, flags, number = struct.unpack('<BBBI', data[41:])
        assert (difficulty, clues, flags, number) == (bank.TIERS.index(record['difficulty']), record['clues'], 0,
                                                     int(record['canonical_sha256'][:8], 16))
    example = json.loads((ROOT/'screen_puzzle.json').read_text(encoding='utf-8'))
    # v1设计稿保留原题，更新产品题库无需重绘历史PNG。/ Keep the original v1 design puzzle; bank updates need not redraw historical PNGs.
    assert exact_cover_solutions(example['grid']) == [example['solution']]
    assert example['id'] == 'PN-SDK-1-' + hashlib.sha256(bank.canonical(example['grid']).encode()).hexdigest()[:8]
    summary = {'png_exports_verified': png_count, 'native_screens': 4, 'puzzles_verified': len(records),
               'difficulty_counts': dict(Counter(r['difficulty'] for r in records)),
               'rating_profile': 2,
               'independent_solver': 'Algorithm X exact cover, limit 2',
               'proof_steps_replayed': trace_count, 'techniques': dict(techniques),
               'binary_bytes': len(payload), 'binary_roundtrip': True,
               'palette': '16 fixed gray levels 0,17,...,255; binary sprite alpha',
               'validation_scope': 'host artifacts only; no firmware or panel validation'}
    (ROOT/'verification.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    print(json.dumps(summary), flush=True)


bank_names = ['reading', 'blink', 'look_up', 'thinking', 'wave_low', 'wave_high']
if __name__ == '__main__':
    main()
