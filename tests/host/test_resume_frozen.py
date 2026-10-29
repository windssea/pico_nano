"""冻结会话完成完整校验并提交精确当前页。 / Frozen sessions finish full verification and commit the exact current page."""
import json
import os
import pathlib
import shutil
import sys
import tempfile
import hashlib
import zlib
from test_resume import fixture, run

def packet_position(data):
    assert data[:4] == b'PNRS' and zlib.crc32(data[:2300]) == int.from_bytes(data[2300:2304], 'little')
    p = data[88:1136]
    return dict(path=p[:1024].split(b'\0')[0].decode(), element=int.from_bytes(p[1024:1032], 'little'),
                offset=int.from_bytes(p[1032:1040], 'little'), run=int.from_bytes(p[1040:1044], 'little'),
                kind=p[1044], version=p[1045], chapter_start=p[1046])

def saved_position(state):
    slots = [p.read_bytes() for p in state.glob('*.epub.*')]
    for b in slots:
        assert b[:4] == b'PNBL' and zlib.crc32(b[:-4]) == int.from_bytes(b[-4:], 'little')
    b = max(slots, key=lambda b: int.from_bytes(b[12:20], 'little'))[20:-4]
    return dict(path=b[68:].decode(), element=int.from_bytes(b[44:52], 'little'), offset=int.from_bytes(b[52:60], 'little'),
                run=int.from_bytes(b[60:64], 'little'), kind=b[42], version=int.from_bytes(b[40:42], 'little'), chapter_start=0)

binary, font = sys.argv[1:3]
with tempfile.TemporaryDirectory() as tmp:
    root = pathlib.Path(tmp)
    local_font = root / 'font.ttf'
    shutil.copyfile(font, local_font)
    fixture(root)
    state = root / 'state'
    run(binary, 'epub', root / 'book.epub', str(local_font), state, root / 'seed', 'seed')
    env = dict(os.environ, PN_RESUME_SELECTED_FONT='1')
    # 保留两个旧槽，推进到两者之外，避免旧包命中同步完成校验。/ Retain both old slots and advance beyond them so a hit cannot finish verification synchronously.
    for i in range(2):
        run(binary, 'epub', root / 'book.epub', str(local_font), state, root / ('advance' + str(i)), 'advance-lock')
    old = {p.name: p.read_bytes() for p in (state / 'reading' / 'resume').glob('*.pnr')}
    wanted = saved_position(state)
    assert len(old) == 2 and all(packet_position(p) != wanted for p in old.values())
    for case in ('cancelled', 'bad-font'):
        case_state = root / case
        shutil.copytree(state, case_state)
        case_env = dict(env, **{'PN_RESUME_FROZEN_CANCEL': '1'} if case == 'cancelled' else {'PN_RESUME_BAD_FONT': '1'})
        result, _ = run(binary, 'epub', root / 'book.epub', str(local_font), case_state, root / (case + '-frames'), 'freeze', case_env)
        assert saved_position(case_state) != wanted, (case, 'progress did not advance')
        if case == 'bad-font':
            assert result['stored'] == 0 and {p.name: p.read_bytes() for p in (case_state / 'reading' / 'resume').glob('*.pnr')} == old, result
        else:
            current = next((case_state / 'reading' / 'resume').glob('*.0.pnr'))
            assert result['stored'] == 1 and packet_position(current.read_bytes()) == saved_position(case_state), result
        print(case, json.dumps(result), flush=True)
    result, _ = run(binary, 'epub', root / 'book.epub', str(local_font), state, root / 'frozen', 'freeze', env)
    assert result['stored'] == 1 and result['position_misses'] >= 2, result
    current = next((state / 'reading' / 'resume').glob('*.0.pnr'))
    assert packet_position(current.read_bytes()) == saved_position(state)
    assert next((state / 'reading' / 'resume').glob('*.1.pnr')).read_bytes() == next(v for k, v in old.items() if '.1.pnr' in k)
    trusted = next((state / 'resume').glob('*.0.pnrt')).read_bytes()
    assert trusted[8:40] == hashlib.sha256(current.read_bytes()).digest()
    events = [json.loads(line) for line in (root / 'frozen' / 'positions.jsonl').read_text().splitlines()]
    candidates = [e for e in events if e['phase'] == 'resume.position.candidate']
    assert {e['slot'] for e in candidates} == {0, 1} and all(e['status'] != 0 for e in candidates)
    phases = [e['phase'] for e in events]
    order = ['epub.progress.confirmed', 'epub.progress.commit', 'resume.write.begin', 'resume.write.root-committed', 'resume.write.committed']
    assert [phases.index(p) for p in order] == sorted(phases.index(p) for p in order), phases
    committed = next(e for e in events if e['phase'] == 'resume.write.committed')
    assert {k: committed[k] for k in saved_position(state)} == saved_position(state)
    reference = root / 'reference'
    shutil.copytree(state, reference)
    shutil.rmtree(reference / 'reading' / 'resume')
    expected, frames = run(binary, 'epub', root / 'book.epub', str(local_font), reference, root / 'expected')
    actual, pixels = run(binary, 'epub', root / 'book.epub', str(local_font), state, root / 'actual')
    assert actual['hits'] >= 1 and actual['neighbor_hits'] == 0 and frames == pixels, actual
    print(json.dumps({'old_slots': {k: packet_position(v) for k, v in old.items()}, 'old_progress': wanted,
                      'committed': saved_position(state), 'commit_order': order, 'frozen': result,
                      'reopen': actual, 'exact_frames': True}), flush=True)
