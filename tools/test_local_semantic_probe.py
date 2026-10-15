"""真实EPUB跨章缓存头筛选、完整缓存与正文/像素对照。 / Verify real-EPUB chapter header filtering, complete caches and body/pixel equivalence."""
import argparse
import json
import os
import pathlib
import posixpath
import re
import shutil
import subprocess
import tempfile
import urllib.parse
import xml.etree.ElementTree as ET
import zipfile
from test_local_samples import ROOT, digest


def chapters(book):
    with zipfile.ZipFile(book) as archive:
        container = ET.fromstring(archive.read('META-INF/container.xml'))
        package = next(e.attrib['full-path'] for e in container.iter() if e.tag.rsplit('}', 1)[-1] == 'rootfile')
        opf = ET.fromstring(archive.read(package))
        items = {e.attrib['id']: posixpath.normpath(posixpath.join(posixpath.dirname(package), urllib.parse.unquote(e.attrib['href'])))
                 for e in opf.iter() if e.tag.rsplit('}', 1)[-1] == 'item'}
        spine = [items[e.attrib['idref']] for e in opf.iter() if e.tag.rsplit('}', 1)[-1] == 'itemref']
        candidates = [(archive.getinfo(path).file_size, i) for i, path in enumerate(spine) if 1000 < archive.getinfo(path).file_size <= 32768]
        index = max(candidates)[1] if candidates else 0
        return spine, index


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=pathlib.Path, required=True)
    parser.add_argument('--probe', type=pathlib.Path, required=True)
    parser.add_argument('--baseline', type=pathlib.Path)
    parser.add_argument('--book', help='Optional exact supplied filename for focused compatibility-cache checks')
    parser.add_argument('--output', type=pathlib.Path, default=ROOT / 'build-dev/local-semantic-probe')
    args = parser.parse_args()
    books = sorted((ROOT / 'tests/testBook').rglob('*.epub'))
    if args.book:
        books = [book for book in books if book.name == args.book]
    font = ROOT / 'tests/testFont/霞鹜文楷.ttf'
    if not books or not font.exists():
        raise ValueError('Requires supplied EPUBs and 霞鹜文楷.ttf')
    identities = {p: digest(p) for p in books + [font]}
    args.output.mkdir(parents=True, exist_ok=True)
    report = {'passed': False, 'books': [], 'scope': 'Host cache I/O/shared renderer; no panel/TF timing qualification'}
    target = args.output / 'report.json'
    target.write_text(json.dumps(report), encoding='utf-8')
    with tempfile.TemporaryDirectory() as temporary:
        root = pathlib.Path(temporary)
        cases = [(book, None) for book in books]
        cases += [(book, chapter) for book in books if book.name.startswith('刀剑神域') for chapter in (1, 79)]
        for i, (original, forced) in enumerate(cases):
            book = root / original.name
            shutil.copy2(original, book)
            spine, chapter = chapters(book)
            if forced is not None:
                if forced >= len(spine) or forced == chapter:
                    continue
                chapter = forced
            other = (chapter + 1) % len(spine)
            assert other != chapter
            def capture(label, binary, state, index, idle):
                state.mkdir(exist_ok=True)
                image = root / f'{i}-{label}.pgm'
                result = subprocess.run([str(binary.resolve()), str(book), str(font), '1', str(image), '44', str(state)],
                    env=dict(os.environ, PN_EPUB_APP_SECTION=str(index), **({'PN_EPUB_CACHE_IDLE': '1'} if idle else {})), capture_output=True, text=True, timeout=180)
                (args.output / f'{i}-{label}.log').write_text(result.stdout + result.stderr, encoding='utf-8')
                assert result.returncode == 0 and 'used=0 live=0' in result.stderr and image.exists(), (original.name, label)
                return image.read_bytes()
            state = root / f'{i}-new-state'
            initial = capture('cold', args.capture, state, chapter, True)
            packets = list((state / 'semantic').glob('*.pnsm'))
            assert len(packets) == 1
            original_packet = packets[0].read_bytes()
            result = subprocess.run([str(args.probe.resolve()), str(book), str(state), spine[chapter], spine[other]], capture_output=True, text=True, timeout=180)
            (args.output / f'{i}-probe.log').write_text(result.stdout + result.stderr, encoding='utf-8')
            match = re.search(r'probe old_bytes=(\d+) new_bytes=(\d+) packet_bytes=(\d+) peak=(\d+) used=(\d+) live=(\d+)', result.stdout)
            assert result.returncode == 0 and match and match[5] == match[6] == '0' and int(match[2]) == 152 and packets[0].read_bytes() == original_packet, original.name
            entry = {'book': original.name, 'chapter': chapter, 'other': other, 'old_bytes': int(match[1]), 'new_bytes': int(match[2]),
                     'packet_bytes': int(match[3]), 'pool_peak_bytes': int(match[4]), 'cold_source_events_equal': True, 'cache_restored': True,
                     'version': int.from_bytes(original_packet[4:8], 'little'), 'windowed': bool(original_packet[112]&1)}
            if args.baseline:
                oldstate = root / f'{i}-old-state'
                before = capture('old-cold', args.baseline, oldstate, chapter, True)
                assert initial == before
                assert list((oldstate / 'semantic').glob('*.pnsm'))[0].read_bytes() == original_packet
                old_cross = capture('old-cross', args.baseline, oldstate, other, False)
                new_cross = capture('new-cross', args.capture, state, other, False)
                assert old_cross == new_cross
                entry['baseline_pixels_and_cache_bytes_equal'] = True
            report['books'].append(entry)
            print(f'PASS semantic probe {original.name} {entry["old_bytes"]} -> {entry["new_bytes"]}', flush=True)
    report['originals_unchanged'] = all(digest(p) == value for p, value in identities.items())
    assert report['originals_unchanged']
    report['passed'] = True
    target.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
