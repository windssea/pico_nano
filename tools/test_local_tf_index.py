"""真实EPUB的独立版本与TF冷/暖像素对照，原件只读。 / Independent-version and TF cold/warm pixel comparisons on real EPUBs, keeping originals read-only."""
import argparse
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import tempfile
import time

from test_local_samples import ROOT, digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=pathlib.Path, required=True)
    parser.add_argument('--baseline', type=pathlib.Path)
    parser.add_argument('--books', type=pathlib.Path, default=ROOT / 'tests/testBook')
    parser.add_argument('--font', type=pathlib.Path, default=ROOT / 'tests/testFont/霞鹜文楷.ttf')
    parser.add_argument('--output', type=pathlib.Path, default=ROOT / 'build-dev/local-tf-index')
    parser.add_argument('--limit', type=int)
    args = parser.parse_args()
    books = sorted(p for p in args.books.rglob('*') if p.is_file() and p.suffix.lower() == '.epub')
    assert books and args.font.is_file(), 'Provide existing books/font paths; -Demo supplies original public samples.'
    if args.limit is not None:
        assert args.limit > 0
        books = books[:args.limit]
    identities = {p: digest(p) for p in [*books, args.font]}
    args.output.mkdir(parents=True, exist_ok=True)
    report = {'passed': False, 'scope': 'Host software; no physical TF, panel, sleep or power measurements', 'independent_baseline': str(args.baseline.resolve()) if args.baseline else None, 'limited': args.limit is not None, 'pages': []}

    with tempfile.TemporaryDirectory(prefix='pn-local-tf-') as folder:
        root = pathlib.Path(folder)
        font = root / args.font.name
        shutil.copy2(args.font, font)

        def capture(label, binary, book, page, state, cache, target):
            env = dict(os.environ, PN_EPUB_TRACE='1')
            env.pop('PN_READING_CACHE', None)
            env.pop('PN_EPUB_INDEX_IDLE', None)
            if cache is not None:
                env.update(PN_READING_CACHE=str(cache), PN_EPUB_INDEX_IDLE='1')
            started = time.perf_counter_ns()
            result = subprocess.run([str(binary.resolve()), str(book), str(font), str(page), str(target.resolve()), '44', str(state)], env=env, capture_output=True, text=True, timeout=180)
            elapsed = time.perf_counter_ns() - started
            target.with_suffix('.log').write_text(result.stdout + result.stderr, encoding='utf-8')
            assert result.returncode == 0 and 'used=0 live=0' in result.stderr, (book.name, page, label, result.stderr[-3000:])
            peak = int(re.search(r'EPUB app status=0 peak=(\d+)', result.stderr)[1])
            assert peak <= 6 * 1024 * 1024
            return target.read_bytes(), result.stdout, {'host_total_ns': elapsed, 'pool_peak': peak}

        for number, original in enumerate(books):
            book = root / original.name
            shutil.copy2(original, book)
            for page in (1, 2, 3):
                state = root / f'state-{number}-{page}'
                cache = root / f'tf-{number}-{page}'
                prefix = args.output / f'{number}-{page}'
                cold, where, cold_stats = capture('cold', args.capture, book, page, state, cache, prefix.with_name(prefix.name + '-cold.pgm'))
                assert list((cache / 'pages').glob('*.pni')) and list((state / 'indexes').glob('*.pnrt'))
                warm, warm_where, warm_stats = capture('warm', args.capture, book, 1, state, cache, prefix.with_name(prefix.name + '-warm.pgm'))
                assert cold == warm and where == warm_where, (original.name, page, 'TF reopen pixels/position differ')
                before_stats = None
                if args.baseline:
                    before, before_where, before_stats = capture('before', args.baseline, book, page, root / f'before-{number}-{page}', None, prefix.with_name(prefix.name + '-before.pgm'))
                    assert before == cold and before_where == where, (original.name, page, 'independent baseline pixels/position differ')
                report['pages'].append({'book': original.name, 'page': page, 'sha256': hashlib.sha256(cold).hexdigest(), 'position': where.strip(), 'cold': cold_stats, 'warm': warm_stats, 'before': before_stats, 'native_pixels_equal': True})
                print(f'PASS TF index {original.name} page={page} independent={bool(args.baseline)} native_pixels_equal=1', flush=True)
    report['originals_unchanged'] = all(digest(p) == value for p, value in identities.items())
    assert report['originals_unchanged']
    report['passed'] = True
    (args.output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
