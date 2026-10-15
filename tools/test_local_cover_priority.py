"""真实书籍封面先显示后落盘的有界队列检查；原件只读。 / Check cover-before-persistence queues using real books, keeping originals read-only."""
import argparse
import json
import os
import pathlib
import re
import shutil
import subprocess
import tempfile
import zlib
from test_local_samples import ROOT, digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=pathlib.Path, required=True)
    parser.add_argument('--baseline', type=pathlib.Path, help='Optional previous cover_priority_validate for exact cache-byte compatibility')
    parser.add_argument('--compression-probe', action='store_true', help='Optional historical lossless-size probe; does not change the product')
    parser.add_argument('--deny-source-cache', action='store_true', help='Reject optional parent allocations during extraction to check direct-read fallback')
    parser.add_argument('--rebind-before-write', action='store_true', help='Reorder resident slots before deferred persistence and compare complete cache files')
    parser.add_argument('--output', type=pathlib.Path, default=ROOT / 'build-dev/local-cover-priority')
    args = parser.parse_args()
    books = sorted(p for p in (ROOT / 'tests/testBook').rglob('*') if p.suffix.lower() in ('.epub', '.txt'))
    if not books:
        raise ValueError('No user reading books')
    identities = {p: digest(p) for p in books}
    args.output.mkdir(parents=True, exist_ok=True)
    report = {'passed': False, 'pages': [], 'scope': 'Software queue/pixel oracle, no real panel timing'}
    target = args.output / 'report.json'
    target.write_text(json.dumps(report), encoding='utf-8')
    with tempfile.TemporaryDirectory() as temporary:
        root = pathlib.Path(temporary)
        for first in range(0, len(books), 6):
            selected = books[first:first + 6]
            copies = []
            for i, original in enumerate(selected):
                copy = root / f'{first+i}-{original.name}'
                shutil.copy2(original, copy)
                copies.append(copy)
            old, new = root / f'{first}-sync', root / f'{first}-staged'
            env = dict(os.environ)
            if args.deny_source_cache:
                env['PN_COVER_FAIL_SOURCE_OPTIONAL'] = '1'
            else:
                env.pop('PN_COVER_FAIL_SOURCE_OPTIONAL', None)
            if args.rebind_before_write:
                env['PN_COVER_REBIND_BEFORE_WRITE'] = '1'
            else:
                env.pop('PN_COVER_REBIND_BEFORE_WRITE', None)
            result = subprocess.run([str(args.binary.resolve()), str(old), str(new), *map(str, copies)], capture_output=True, text=True, timeout=300, env=env)
            (args.output / f'{first}.log').write_text(result.stdout + result.stderr, encoding='utf-8')
            match = re.search(r'priority books=(\d+) visible_steps=(\d+) all_steps=(\d+) writes=(\d+) syncs=(\d+) extracts=(\d+) peak=(\d+) used=(\d+) live=(\d+)', result.stdout)
            assert result.returncode == 0 and match and match[8] == match[9] == '0', first
            if args.rebind_before_write:
                assert 'priority resident_rebind_before_write=1 offpage_resume=1' in result.stdout
            before = {p.name: p.read_bytes() for p in old.glob('*.pnc')}
            after = {p.name: p.read_bytes() for p in new.glob('*.pnc')}
            assert len(before) == len(selected) and before == after, ('cache bytes', first)
            source = re.search(r'source reads=(\d+) bytes=(\d+) cache_peak=(\d+) bound_sources=(\d+)', result.stdout)
            if source:
                assert int(source[3]) <= 36 * 1024
            optional = re.search(r'source optional_requests=(\d+) optional_denied=(\d+)', result.stdout)
            if optional:
                assert int(optional[2]) == (int(optional[1]) if args.deny_source_cache else 0)
            if args.baseline:
                baseline_sync, baseline_staged = root / f'{first}-baseline-sync', root / f'{first}-baseline-staged'
                baseline = subprocess.run([str(args.baseline.resolve()), str(baseline_sync), str(baseline_staged), *map(str, copies)], capture_output=True, text=True, timeout=300)
                (args.output / f'{first}-baseline.log').write_text(baseline.stdout + baseline.stderr, encoding='utf-8')
                assert baseline.returncode == 0, ('baseline', first)
                assert after == {p.name: p.read_bytes() for p in baseline_staged.glob('*.pnc')}, ('baseline cache bytes', first)
            compression = []
            for data in (after.values() if args.compression_probe else ()):
                if data[10] != 2:
                    continue
                # 仅测量已验证4bpp像素的无损编码大小，产品缓存格式不变。/ Measure lossless sizes of verified 4bpp pixels without changing the product cache format.
                pixels = data[-23556:-4]
                assert len(pixels) == 23552
                runs, last, count = 0, None, 0
                for value in pixels:
                    if value != last or count == 255:
                        runs += 1
                        last, count = value, 0
                    count += 1
                compression.append({'pixel_bytes': len(pixels), 'zlib1_bytes': len(zlib.compress(pixels, 1)),
                    'zlib6_bytes': len(zlib.compress(pixels, 6)), 'byte_rle_bytes': 2 * runs})
            report['pages'].append({'books': [p.name for p in selected], 'visible_steps': int(match[2]), 'all_steps': int(match[3]),
                'writes_after_visible_only': int(match[4]), 'syncs_after_visible_only': int(match[5]), 'unique_epub_extracts': int(match[6]),
                'pool_peak_bytes': int(match[7]), 'cache_bytes_equal': True, 'pixels_and_metadata_equal': True,
                'source_port_reads': int(source[1]) if source else None, 'source_port_bytes': int(source[2]) if source else None,
                'source_cache_peak': int(source[3]) if source else None, 'bound_sources': int(source[4]) if source else None,
                'optional_source_requests': int(optional[1]) if optional else None, 'optional_source_denied': int(optional[2]) if optional else None,
                'baseline_bytes_equal': True if args.baseline else None, 'resident_rebind_before_write': args.rebind_before_write, 'offpage_resume': args.rebind_before_write, 'compression_size_probe_only': compression})
            print(f'PASS cover priority page={first//6} books={len(selected)} visible={match[2]} all={match[3]}', flush=True)
    report['originals_unchanged'] = all(digest(p) == value for p, value in identities.items())
    assert report['originals_unchanged']
    report['passed'] = True
    target.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
