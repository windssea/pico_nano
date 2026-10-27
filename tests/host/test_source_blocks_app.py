"""重启分块证明、完整像素与缺件退路。 / Restart block proofs, exact pixels and missing-piece fallback."""
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import zipfile
import zlib
from test_resume import fixture, run

binary, font = sys.argv[1:]
with tempfile.TemporaryDirectory() as tmp:
    root = pathlib.Path(tmp)
    fixture(root)
    book = root / 'book.epub'
    # 固定时间/STORED允许同stat替换；故意漏列CSS且隔开尾目录，不能让结构块偶然覆盖它。
    # Fixed times/STORED allow same-stat replacement; deliberately omit CSS from the manifest and separate it from the central directory.
    with zipfile.ZipFile(book) as z:
        entries = [(n, z.read(n)) for n in z.namelist()]
    def write_book(changed=False):
        with zipfile.ZipFile(book, 'w') as z:
            for name, data in entries:
                if name == 'a.xhtml':
                    data = data.replace(b'<body>', b'<head><link rel="stylesheet" href="style.data"/></head><body>', 1)
                if changed and name == 'a.xhtml':
                    data = data.replace('一'.encode(), '海'.encode())
                z.writestr(zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0)), data)
            z.writestr(zipfile.ZipInfo('unused.bin', (2026, 1, 1, 0, 0, 0)), bytes(2 * 1024 * 1024))
            z.writestr(zipfile.ZipInfo('style.data', (2026, 1, 1, 0, 0, 0)), b'p { text-align: left; }')
            z.writestr(zipfile.ZipInfo('unused-tail.bin', (2026, 1, 1, 0, 0, 0)), bytes(32768))
    write_book()
    original = book.read_bytes()
    seed = root / 'seed'
    seeded, _ = run(binary, 'epub', book, font, seed, root / 'seed-frames', 'seed',
                    dict(os.environ, PN_BOOTSTRAP_SEED='1', PN_RESUME_SEMANTIC='1', PN_BLOCK_INTERLEAVE='1'))
    assert seeded['block_stored'] == 1, seeded
    packet = next((seed / 'reading/resume').glob('*.pnsv'))
    trusted = next((seed / 'resume').glob('*.pnst'))
    payload, proof = packet.read_bytes(), trusted.read_bytes()
    assert payload[:8] == b'PNSBLK1\0' and payload[8:40] == hashlib.sha256(original).digest()
    assert int.from_bytes(payload[40:48], 'little') == len(original)
    assert int.from_bytes(payload[48:52], 'little') == 16384
    assert int.from_bytes(payload[60:64], 'little') == zlib.crc32(payload[:60])
    hashes = b''.join(hashlib.sha256(original[i:i + 16384]).digest() for i in range(0, len(original), 16384))
    assert payload[64:] == hashes and proof[8:40] == hashlib.sha256(payload).digest()
    assert int.from_bytes(proof[72:], 'little') == zlib.crc32(proof[:72])
    reference = root / 'reference'
    shutil.copytree(seed, reference)
    shutil.rmtree(reference / 'reading/resume')
    _, expected = run(binary, 'epub', book, font, reference, root / 'reference-frames')
    env = dict(os.environ, PN_BOOTSTRAP_TRUSTED='1', PN_BLOCK_EXPECTED=hashlib.sha256(original).hexdigest(), PN_RESUME_ENTRY='1')
    for case in ('restart', 'idle-takeover', 'idle-missing', 'missing', 'root-missing', 'corrupt', 'root-corrupt', 'truncated', 'backup', 'stale-temp', 'no-bootstrap'):
        state = root / case
        shutil.copytree(seed, state)
        p, t = state / packet.relative_to(seed), state / trusted.relative_to(seed)
        if case in ('missing', 'idle-missing'): p.unlink()
        elif case == 'root-missing': t.unlink()
        elif case == 'corrupt': p.write_bytes(payload[:-1] + bytes([payload[-1] ^ 1]))
        elif case == 'root-corrupt': t.write_bytes(proof[:-1] + bytes([proof[-1] ^ 1]))
        elif case == 'truncated': p.write_bytes(payload[:64])
        elif case == 'backup':
            p.rename(p.with_suffix('.pnsv.bak'))
            t.rename(t.with_suffix('.pnst.bak'))
        elif case == 'stale-temp':
            p.with_suffix('.pnsv.tmp').write_bytes(b'incomplete')
            t.with_suffix('.pnst.tmp').write_bytes(b'incomplete')
        elif case == 'no-bootstrap':
            for f in state.rglob('*.pnb*'): f.unlink()
        case_env=dict(env,PN_BLOCK_IDLE='1') if case.startswith('idle-') else env
        result, frames = run(binary, 'epub', book, font, state, root / (case + '-frames'), env=case_env)
        hit = case in ('restart', 'idle-takeover', 'backup', 'stale-temp', 'no-bootstrap')
        assert result['block_hits'] == int(hit) and (result['identity_steps'] == 0) == hit, (case, result)
        assert frames == expected, (case, result)
        print(case, result, flush=True)
    # 当前包命中、下一页包缺失和预排未完成的真实回退：FIRST NEXT不加载全页表，像素仍完全相同。
    # Real fallback with a current-packet hit, missing next packet and unfinished prefetch: first NEXT loads no whole-page index and retains identical pixels.
    state = root / 'next-priority';shutil.copytree(seed, state)
    for path in state.rglob('*.1.pnr*'): path.unlink()
    result, frames = run(binary, 'epub', book, font, state, root / 'next-priority-frames',
                        env=dict(env, PN_RESUME_NEXT_PRIORITY='1', PN_RESUME_YIELD='1', PN_RESUME_OVERLAP='1', PN_RESUME_OVERLAP_STEPS='1'))
    assert result['next_index_binds'] == 0 and frames == expected, result
    print('next-priority', result, flush=True)
    # 漏列样式被改且ZIP目录完全不变时，旧语义/续读包也不得绕过实际依赖检查。
    # Undeclared CSS changed without updating the ZIP directory must not bypass dependency checks via old semantic/resume packets.
    with zipfile.ZipFile(book) as z: info = z.getinfo('style.data')
    corrupt = bytearray(original)
    at = info.header_offset + 30 + len(info.filename.encode()) + len(info.extra)
    corrupt[at] ^= 1
    book.write_bytes(corrupt)
    state = root / 'changed-style';shutil.copytree(seed, state)
    frames_dir = root / 'changed-style-frames';frames_dir.mkdir()
    failed = subprocess.run([binary, 'epub', str(book), font, str(state), str(frames_dir), 'read'], env=env, capture_output=True, text=True, timeout=120)
    assert failed.returncode != 0 and not list(frames_dir.glob('*.raw')), (failed.stdout, failed.stderr)
    rejected = json.loads(failed.stdout.strip().splitlines()[-1])
    assert rejected['status'] != 0 and rejected['block_rebuilds'] == 1 and rejected['used'] == rejected['live'] == 0, rejected
    book.write_bytes(original)
    # 真实同路径替换须自动完整导入，旧续读画面不得接管新内容。
    # A real same-path replacement must reimport completely; old resume pixels must never take over new content.
    timestamp = book.stat()
    write_book(True)
    assert book.stat().st_size == len(original) and book.read_bytes() != original
    os.utime(book, ns=(timestamp.st_atime_ns, timestamp.st_mtime_ns))
    state = root / 'replacement'
    shutil.copytree(seed, state)
    result, frames = run(binary, 'epub', book, font, state, root / 'replacement-frames', env=env)
    _, fresh = run(binary, 'epub', book, font, root / 'fresh-state', root / 'fresh-frames')
    assert frames == fresh and result['reimported'] and result['block_rebuilds'] == 1 and result['hits'] == 0, result
    print('same-size same-mtime replacement', result, flush=True)
    # 大章、多章与图片依赖使用同一真实入口；只验证最终像素，不建立中间性能矩阵。
    # Large single chapters, many chapters and image dependencies share the real entry; check final pixels without an intermediate performance matrix.
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / 'tools'))
    from test_local_book_index import fixture as large_fixture
    for label in ('multi', 'single', 'image'):
        if label == 'multi': large_fixture(book, 48, 12000)
        elif label == 'single': large_fixture(book, 1, 576000)
        else:
            fixture(root)
            with zipfile.ZipFile(book) as z:
                contents = [(n, z.read(n)) for n in z.namelist()]
            with zipfile.ZipFile(book, 'w', compression=zipfile.ZIP_DEFLATED) as z:
                for name, data in contents:
                    if name == 'a.xhtml': data = data.replace(b'<body>', b'<body><img src="picture.png"/>', 1)
                    if name == 'book.opf': data = data.replace(b'</manifest>', b'<item id="im" href="picture.png" media-type="image/png"/></manifest>')
                    z.writestr(name, data, compress_type=zipfile.ZIP_STORED if name == 'mimetype' else zipfile.ZIP_DEFLATED)
                z.writestr('picture.png', (pathlib.Path(__file__).resolve().parents[1] / 'fixtures/image-black.png').read_bytes())
        seed = root / (label + '-seed')
        seeded, _ = run(binary, 'epub', book, font, seed, root / (label + '-seed-frames'), 'seed-first' if label == 'image' else 'seed', dict(os.environ, PN_BOOTSTRAP_SEED='1', PN_RESUME_SEMANTIC='1'))
        assert seeded['block_stored'] == 1, seeded
        reference = root / (label + '-reference')
        shutil.copytree(seed, reference)
        shutil.rmtree(reference / 'reading/resume')
        _, expected = run(binary, 'epub', book, font, reference, root / (label + '-reference-frames'))
        env = dict(os.environ, PN_BOOTSTRAP_TRUSTED='1', PN_BLOCK_EXPECTED=hashlib.sha256(book.read_bytes()).hexdigest(), PN_RESUME_ENTRY='1')
        result, frames = run(binary, 'epub', book, font, seed, root / (label + '-frames'), env=env)
        assert frames == expected and result['block_hits'] == 1 and result['identity_steps'] == 0, (label, result)
        print(label, result, flush=True)
