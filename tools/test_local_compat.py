"""用户失败章节的独立字节/事件/像素及缓存验证，原件只读。 / Independent byte/event/pixel and cache checks for failing user chapters, with read-only originals."""
import argparse
import json
import os
import pathlib
import re
import shutil
import struct
import subprocess
import tempfile
import xml.etree.ElementTree as ET
import zipfile
import zlib
from html.entities import name2codepoint
from test_local_samples import ROOT, digest

# 参考端只识别测试原章实际使用的标准标记，未知尖括号保持正文。/ The reference recognizes standard tags used by the actual chapters, preserving unknown angle text.
TAGS = rb'html|head|title|meta|link|body|p|div|span|h[1-6]|a|b|strong|em|i|u|s|sup|sub|br|hr|pre|blockquote|img|style|script|ul|ol|li|table|tr|td|th|tbody|svg|image|g|path|rect|circle|ellipse|line|polyline|polygon|defs|use|text|tspan'
PROTECTED = re.compile(rb'(<\?.*?\?>|<!--.*?-->|<!\[CDATA\[.*?\]\]>|<!DOCTYPE[^>]*>)', re.S)


def reference(raw):
    """独立整块正则oracle，仅用于用户已知UTF8章节；不作为产品解析器。 / Independent whole-buffer regex oracle for actual UTF-8 chapters, never a product parser."""
    result = []
    for part in PROTECTED.split(raw):
        if PROTECTED.fullmatch(part):
            result.append(part)
            continue
        part = re.sub(rb'&(nbsp|ensp|emsp|thinsp|ndash|mdash|hellip|lsquo|rsquo|ldquo|rdquo|bull|middot|copy|reg|trade|yen|euro|times|divide|laquo|raquo|shy);', lambda match: f'&#{name2codepoint[match[1].decode()]};'.encode(), part)
        part = re.sub(rb'&(?!(?:amp|lt|gt|apos|quot|#[0-9]+|#x[0-9a-fA-F]+);)', b'&amp;', part)
        part = re.sub(rb'<(?!(?:/?(?:' + TAGS + rb')(?=[\s/>]))|[!?])', b'&lt;', part)
        result.append(part)
    normalized = b''.join(result)
    ET.fromstring(normalized)
    return normalized


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--filter', type=pathlib.Path, required=True)
    parser.add_argument('--validate', type=pathlib.Path, required=True)
    parser.add_argument('--capture', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, default=ROOT / 'build-dev/local-compat')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'report.json').write_text('{"passed": false, "state": "running; inspect the log if interrupted"}\n', encoding='utf-8')
    fonts = sorted((ROOT / 'tests/testFont').glob('*.[tT][tT][fF]'))
    if not fonts:
        raise ValueError('Requires user fonts')
    report = {'scope': 'Actual user chapters; host shared renderer, no panel qualification', 'books': []}
    with tempfile.TemporaryDirectory() as temporary:
        root = pathlib.Path(temporary)
        font_copies = []
        for font in fonts:
            copied = root / font.name
            shutil.copy2(font, copied)
            font_copies.append(copied)
        for book in sorted((ROOT / 'tests/testBook').glob('*.epub')):
            identity = digest(book)
            repairs = {}
            with zipfile.ZipFile(book) as archive:
                for name in archive.namelist():
                    if not name.lower().endswith(('.html', '.xhtml', '.htm')):
                        continue
                    raw = archive.read(name)
                    try:
                        ET.fromstring(raw)
                    except ET.ParseError:
                        repairs[name] = reference(raw)
                if not repairs:
                    continue
                original = root / f'original-{len(report["books"])}.epub'
                normalized = root / f'normalized-{len(report["books"])}.epub'
                shutil.copy2(book, original)
                with zipfile.ZipFile(normalized, 'w') as target:
                    for member in archive.infolist():
                        target.writestr(member, repairs.get(member.filename, archive.read(member.filename)))
            entry = {'book': book.name, 'sha256': identity, 'chapters': [], 'captures': []}
            report['books'].append(entry)
            for ci, (name, expected) in enumerate(repairs.items()):
                raw_file = root / 'chapter.xml'
                with zipfile.ZipFile(original) as archive:
                    raw_file.write_bytes(archive.read(name))
                for chunk in (1, 17, 4096):
                    result = subprocess.run([str(args.filter.resolve()), str(raw_file)], env=dict(os.environ, PN_COMPAT_CHUNK=str(chunk)), capture_output=True, timeout=60)
                    assert result.returncode == 0 and result.stdout == expected, (name, chunk, result.stderr)
                entry['chapters'].append({'path': name, 'chunks': [1, 17, 4096], 'full_normalized_bytes_equal': True})
            validations = []
            for label, path in [('original', original), ('reference', normalized)]:
                result = subprocess.run([str(args.validate.resolve()), str(path)], capture_output=True, text=True, timeout=180)
                (args.output / f'{len(report["books"])}-{label}-events.log').write_text(result.stdout + result.stderr, encoding='utf-8')
                assert result.returncode == 0, (label, result.stdout[-1500:], result.stderr)
                validations.append(dict(re.findall(r'events index=(\d+) digest=([0-9a-f]+)', result.stdout)))
            assert validations[0] == validations[1]
            entry['all_chapter_event_digests_equal'] = True
            with zipfile.ZipFile(original) as archive:
                container = ET.fromstring(archive.read('META-INF/container.xml'))
                package = next(e.attrib['full-path'] for e in container.iter() if e.tag.endswith('rootfile'))
                opf = ET.fromstring(archive.read(package))
                from test_epub_sample import resolve
                manifest = {e.attrib['id']: e.attrib for e in opf.iter() if e.tag.endswith('item')}
                spine = [resolve(package, manifest[e.attrib['idref']]['href']) for e in opf.iter() if e.tag.endswith('itemref')]
            bad_indices = [spine.index(name) for name in repairs]
            selected = {*bad_indices[:2], *[i for i in bad_indices if spine[i].endswith(('079.html', '231.html'))]}
            def capture(label, path, chapter, font, state=None, idle=False, pages=1):
                destination = args.output / f'{len(report["books"])}-{label}-{chapter}-{font.stem}.pgm'
                environment = dict(os.environ, PN_EPUB_APP_SECTION=str(chapter), PN_EPUB_TRACE='1')
                if idle:
                    environment['PN_EPUB_CACHE_IDLE'] = '1'
                command = [str(args.capture.resolve()), str(path), str(font), str(pages), str(destination.resolve()), '44']
                if state:
                    command.append(str(state))
                result = subprocess.run(command, env=environment, capture_output=True, text=True, timeout=180)
                (destination.with_suffix('.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
                assert result.returncode == 0 and 'used=0 live=0' in result.stderr, (label, chapter, result.stderr[-2000:])
                return destination.read_bytes(), result.stdout + result.stderr
            for chapter in bad_indices:
                for fi, font in enumerate(font_copies):
                    if fi and chapter not in selected:
                        continue
                    actual, actual_log = capture('actual', original, chapter, font)
                    expected, log = capture('reference', normalized, chapter, font)
                    assert actual == expected and 'phase=epub.compat ' not in log
                    peak = re.search(r'EPUB app status=0 peak=(\d+) used=0 live=0', actual_log)
                    entry['captures'].append({'spine': chapter, 'font': fonts[fi].name, 'full_pixels_equal': True, 'pool_peak_bytes': int(peak[1])})
            entry['later_pages'] = []
            for chapter, pages in ((7, 4), (79, 60), (231, 24)):
                if chapter not in bad_indices:
                    continue
                actual, actual_log = capture(f'later-actual-{pages}', original, chapter, font_copies[0], pages=pages)
                expected, expected_log = capture(f'later-reference-{pages}', normalized, chapter, font_copies[0], pages=pages)
                assert actual == expected and re.findall(r'^path=.*$', actual_log, re.M) == re.findall(r'^path=.*$', expected_log, re.M)
                entry['later_pages'].append({'chapter': chapter, 'pages': pages, 'full_pixels_and_position_equal': True})
            entry['constrained_pools'] = []
            for chapter in sorted(selected):
                for budget in (1024*1024, 2*1024*1024, 4*1024*1024):
                    destination = args.output / f'{len(report["books"])}-budget-{chapter}-{budget}.pgm'
                    result = subprocess.run([str(args.capture.resolve()), str(original), str(font_copies[0]), '1', str(destination.resolve()), '44'],
                        env=dict(os.environ, PN_EPUB_APP_SECTION=str(chapter), PN_EPUB_APP_BUDGET=str(budget)), capture_output=True, text=True, timeout=180)
                    (destination.with_suffix('.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
                    stats = re.search(r'EPUB app status=(\d+) peak=(\d+) used=0 live=0', result.stderr)
                    assert stats and int(stats[1]) in (0, 5) and int(stats[2]) <= budget and result.returncode == int(stats[1] != '0')
                    entry['constrained_pools'].append({'chapter': chapter, 'budget': budget, 'status': int(stats[1]), 'peak': int(stats[2]), 'pool_cleanup': True})
            entry['cache_checks'] = []
            for chapter in dict.fromkeys((0, *bad_indices[:2])):
                state = root / f'state-{len(report["books"])}-{chapter}'
                state.mkdir()
                cold, _ = capture('cold', original, chapter, font_copies[0], state, True)
                packets = list((state / 'semantic').glob('*.pnsm'))
                assert len(packets) == 1
                version = struct.unpack_from('<I', packets[0].read_bytes(), 4)[0]
                assert version == (2 if chapter in bad_indices else 1), (chapter, version)
                warm, log = capture('warm', original, chapter, font_copies[0], state)
                assert warm == cold and 'phase=epub.model-load status=0' in log and 'phase=epub.compat ' not in log and 'phase=content.stream-sha status=0' in log
                valid_packet = packets[0].read_bytes()
                data = bytearray(valid_packet);data[100] ^= 1;packets[0].write_bytes(data)
                rebuilt, log = capture('corrupt', original, chapter, font_copies[0], state)
                assert rebuilt == cold and 'phase=epub.model-build status=0' in log
                data = bytearray(valid_packet);struct.pack_into('<I', data, 4, 99);struct.pack_into('<I', data, 148, zlib.crc32(data[:148]));packets[0].write_bytes(data)
                rebuilt, log = capture('unknown-version', original, chapter, font_copies[0], state)
                assert rebuilt == cold and 'phase=epub.model-build status=0' in log and 'phase=epub.model-load status=0' not in log
                entry['cache_checks'].append({'chapter': chapter, 'version': version, 'warm_pixels_equal': True, 'corruption_rebuild_equal': True, 'unknown_version_rebuild_equal': True})
            # 在真实失败章的运行副本注入CRC错误/缺失结束根；不能因词法容错提交半页。/ Inject CRC failure and a missing closing root into a copied failing chapter; lexical recovery must never commit a partial page.
            failure_path = next(iter(repairs))
            entry['rejected_faults'] = []
            for fault in ('crc', 'truncated-root'):
                broken = root / f'{fault}.epub'
                with zipfile.ZipFile(original) as source, zipfile.ZipFile(broken, 'w') as target:
                    for member in source.infolist():
                        raw = source.read(member.filename)
                        if member.filename == failure_path:
                            member.compress_type = zipfile.ZIP_STORED
                            if fault == 'truncated-root':
                                at = raw.rfind(b'</html>')
                                assert at > 0
                                raw = raw[:at]
                        target.writestr(member, raw)
                if fault == 'crc':
                    with zipfile.ZipFile(broken) as archive:
                        member = archive.getinfo(failure_path)
                        raw = archive.read(member)
                    position = next(i for i in range(len(raw)-1, max(0, len(raw)-128), -1) if raw[i] in b' \r\n\t')
                    data = bytearray(broken.read_bytes())
                    name_size, extra_size = struct.unpack_from('<HH', data, member.header_offset + 26)
                    at = member.header_offset + 30 + name_size + extra_size + position
                    data[at] = ord('\t') if data[at] != ord('\t') else ord(' ')
                    broken.write_bytes(data)
                result = subprocess.run([str(args.validate.resolve()), str(broken)], capture_output=True, text=True, timeout=180)
                (args.output / f'{len(report["books"])}-fault-{fault}.log').write_text(result.stdout + result.stderr, encoding='utf-8')
                failed = re.findall(r'chapter index=(\d+) status=10 .*?path=([^\n]+)', result.stdout)
                assert result.returncode == 1 and failed == [(str(spine.index(failure_path)), failure_path)] and 'used=0 live=0 leases=0' in result.stdout
                destination = args.output / f'{len(report["books"])}-fault-{fault}.pgm'
                result = subprocess.run([str(args.capture.resolve()), str(broken), str(font_copies[0]), '1', str(destination.resolve()), '44'],
                    env=dict(os.environ, PN_EPUB_APP_SECTION=str(spine.index(failure_path))), capture_output=True, text=True, timeout=180)
                assert result.returncode == 1 and 'EPUB app status=10' in result.stderr and 'used=0 live=0' in result.stderr and not destination.exists()
                entry['rejected_faults'].append({'fault': fault, 'status': 10, 'no_partial_page': True, 'pool_cleanup': True})
            entry['original_unchanged'] = digest(book) == identity
            assert entry['original_unchanged']
            print(f'PASS {book.name}: repaired={len(repairs)}, event chapters={len(validations[0])}, render pairs={len(entry["captures"])}, v1/v2 cache checks={len(entry["cache_checks"])}', flush=True)
    report['passed'] = True
    (args.output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
