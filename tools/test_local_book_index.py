"""同源整书索引软件观察器；主机耗时/逻辑IO不是S3或TF实测。 / Host whole-book observer; time/logical I/O are not S3/TF measurements."""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import tempfile
import zipfile
import struct
import zlib


def canonical_table(path, state):
    """独立验证追加链，再导出与v2逐字节可比的有序逻辑表。 / Independently validate the chain and export a byte-comparable v2 logical table."""
    data = path.read_bytes()
    if data[:8] == b'PNPI\x02\0\0\0' and len(data) == 80 + int.from_bytes(data[72:76], 'little') * 80:
        return data
    roots, = (state / 'indexes').glob('*.pnrt')
    root = roots.read_bytes()
    assert len(root) == 140 and root[:8] == b'PNTR\x01\0\0\0' and zlib.crc32(root[:136]) == int.from_bytes(root[136:], 'little')
    entries, at, sequence, previous = {}, 0, 0, bytes(32)
    identity = data[8:72]
    assert root[8:72] == identity
    while at < len(data):
        legacy = at == 0 and data[:8] == b'PNPI\x02\0\0\0'
        size = 80 if legacy else 128
        header = data[at:at + size]
        assert header[8:72] == identity and zlib.crc32(header[:-4]) == int.from_bytes(header[-4:], 'little')
        count = int.from_bytes(header[72:76], 'little')
        if not legacy:
            sequence += 1
            assert header[:8] == b'PNPL\x03\0\0\0' and 0 < count <= 512
            assert int.from_bytes(header[76:80], 'little') == sequence and header[80:112] == previous
            assert int.from_bytes(header[112:120], 'little') == at and header[120:124] == bytes(4)
        end = at + size + count * 80
        last = None
        for pos in range(at + size, end, 80):
            record = data[pos:pos + 80]
            assert len(record) == 80 and zlib.crc32(record[:76]) == int.from_bytes(record[76:], 'little')
            key = (int.from_bytes(record[:4], 'little'), int.from_bytes(record[8:16], 'little'))
            assert last is None or key > last
            last, entries[key] = key, record
        previous = hashlib.sha256(data[at:end]).digest()
        if not legacy:
            tail = data[end:end + 48]
            assert tail[:32] == previous and tail[32:36] == b'PNLE' and int.from_bytes(tail[36:40], 'little') == end + 48 - at
            assert int.from_bytes(tail[40:44], 'little') == sequence and zlib.crc32(tail[:44]) == int.from_bytes(tail[44:], 'little')
            end += 48
        at = end
    assert at == len(data) and previous in (root[72:104], root[104:136])
    header = b'PNPI\x02\0\0\0' + identity + struct.pack('<I', len(entries))
    return header + struct.pack('<I', zlib.crc32(header)) + b''.join(entries[k] for k in sorted(entries))


def identity(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def fixture(path, chapters, chars):
    """原创CC0压力文本，不复制小说。 / Original CC0 stress text, never copied from a novel."""
    with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('mimetype', 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
        archive.writestr('META-INF/container.xml', '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        items = ''.join(f'<item id="c{i}" href="c{i}.xhtml" media-type="application/xhtml+xml"/>' for i in range(chapters))
        spine = ''.join(f'<itemref idref="c{i}"/>' for i in range(chapters))
        archive.writestr('book.opf', f'<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="3.0" unique-identifier="u"><metadata><d:title>Original indexing stress fixture</d:title><d:identifier id="u">CC0-original-stress</d:identifier></metadata><manifest>{items}</manifest><spine>{spine}</spine></package>')
        for chapter in range(chapters):
            text = ''.join(chr(0x4e00 + (i * 37 + chapter * 131) % 4096) for i in range(chars))
            body = ''.join('<p>' + text[j:j + 300] + '</p>' for j in range(0, len(text), 300))
            archive.writestr(f'c{chapter}.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml"><body>' + body + '</body></html>')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=pathlib.Path)
    parser.add_argument('--baseline', type=pathlib.Path)
    parser.add_argument('--migrate-baseline', action='store_true', help='Also maintain the old binary cache with the new binary and verify identical logical records and frame')
    parser.add_argument('--book', type=pathlib.Path)
    parser.add_argument('--font', required=True, type=pathlib.Path)
    parser.add_argument('--output', required=True, type=pathlib.Path)
    parser.add_argument('--chapters', type=int, default=480)
    parser.add_argument('--chars', type=int, default=6000)
    parser.add_argument('--resume-after', type=int, help='Additionally close an unshown candidate at this page, then reopen and compare the completed table/frame')
    args = parser.parse_args()
    assert args.font.is_file() and 1 <= args.chapters <= 1024 and 300 <= args.chars <= 3000000
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='pn-full-index-') as directory:
        root = pathlib.Path(directory)
        source = args.book
        if source is None:
            source = root / 'original-stress.epub'
            fixture(source, args.chapters, args.chars)
        before = {p: identity(p) for p in (source, args.font)}
        book, font = root / 'input.epub', root / 'font.ttf'
        shutil.copy2(source, book)
        shutil.copy2(args.font, font)
        report = {'scope': 'Host core, no product waiting, physical TF, panel or power measurement', 'generated': args.book is None, 'inputs': {p.name: value for p, value in before.items()}, 'runs': {}}
        for name, binary in ([('before', args.baseline)] if args.baseline else []) + [('after', args.binary)]:
            out = args.output / name
            out.mkdir()
            state, stats = root / name, out / 'stats.json'
            with (out / 'compile.log').open('w', encoding='utf-8') as log:
                result = subprocess.run([str(binary.resolve()), str(book), str(font), str(state), str(stats.resolve())], stdout=log, stderr=log, timeout=900)
            assert result.returncode == 0, (name, result.returncode, out / 'compile.log')
            values = json.loads(stats.read_text())
            # 嵌套阶段单列，不求和冒充总耗时。/ Keep nested phases separate; their sum is not total elapsed time.
            values['phases'] = {}
            for line in (out / 'compile.log').read_text(encoding='utf-8').splitlines():
                if line.startswith('phase='):
                    fields = dict(part.split('=', 1) for part in line.split())
                    phase = fields.pop('phase')
                    values['phases'][phase] = {key: int(value) for key, value in fields.items()}
            assert values['complete'] and values['status'] == 0 and values['used'] == values['live'] == 0 and values['pool_peak'] <= 6 * 1024 * 1024
            table, = (state / 'reading/pages').glob('*.pni')
            shutil.copy2(table, out / 'pages.pni')
            logical = canonical_table(table, state)
            (out / 'logical.pni').write_bytes(logical)
            shutil.copy2(state / 'first.pgm', out / 'first.pgm')
            values.update(table_sha=hashlib.sha256(logical).hexdigest(), storage_sha=identity(table), storage_bytes=table.stat().st_size, frame_sha=identity(state / 'first.pgm'), binary_sha=identity(binary))
            report['runs'][name] = values
            print(name, json.dumps(values), flush=True)
        if args.baseline:
            first, last = report['runs']['before'], report['runs']['after']
            assert all(first[key] == last[key] for key in ('pages', 'table_sha', 'frame_sha'))
        if args.migrate_baseline:
            assert args.baseline
            state, out = root / 'before', args.output / 'migration'
            out.mkdir()
            with (out / 'maintain.log').open('w', encoding='utf-8') as log:
                result = subprocess.run([str(args.binary.resolve()), str(book), str(font), str(state), str((out / 'stats.json').resolve()), 'maintain'], stdout=log, stderr=log, timeout=900)
            assert result.returncode == 0
            values = json.loads((out / 'stats.json').read_text())
            table, = (state / 'reading/pages').glob('*.pni')
            assert values['complete'] and values['used'] == values['live'] == 0
            assert hashlib.sha256(canonical_table(table, state)).hexdigest() == first['table_sha'] and identity(state / 'first.pgm') == first['frame_sha']
            values.update(storage_bytes=table.stat().st_size, storage_sha=identity(table), table_frame_equal=True)
            assert values['storage_bytes'] <= first['storage_bytes']
            shutil.copy2(table, out / 'pages.pni')
            report['migration'] = values
            print('migration', json.dumps(values), flush=True)
        if args.resume_after:
            assert 0 < args.resume_after < report['runs']['after']['pages']
            state = root / 'resumed'
            out = args.output / 'resumed'
            out.mkdir()
            for label, action in [('partial', str(args.resume_after)), ('complete', 'resume')]:
                stats = out / (label + '.json')
                with (out / (label + '.log')).open('w', encoding='utf-8') as log:
                    result = subprocess.run([str(args.binary.resolve()), str(book), str(font), str(state), str(stats.resolve()), action], stdout=log, stderr=log, timeout=900)
                assert result.returncode == 0
                values = json.loads(stats.read_text())
                assert values['used'] == values['live'] == 0
                if label == 'partial':
                    assert not values['complete'] and not list(state.glob('*.a')) and not list(state.glob('*.b'))
                else:
                    table, = (state / 'reading/pages').glob('*.pni')
                    assert values['complete'] and values['pages'] == report['runs']['after']['pages']
                    assert hashlib.sha256(canonical_table(table, state)).hexdigest() == report['runs']['after']['table_sha'] and identity(state / 'first.pgm') == report['runs']['after']['frame_sha']
                    shutil.copy2(table, out / 'pages.pni')
                    shutil.copy2(state / 'first.pgm', out / 'first.pgm')
            report['resume_table_frame_equal'] = True
        assert all(identity(p) == value for p, value in before.items())
        report['originals_unchanged'] = True
        (args.output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
