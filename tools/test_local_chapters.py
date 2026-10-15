"""逐本逐章调用实际EPUB正文解析器，保留全部失败章节。 / Run the actual EPUB body parser for every supplied chapter, retaining all failures."""
import argparse
import json
import pathlib
import re
import subprocess
from test_local_samples import ROOT, digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, default=ROOT / 'build-dev/local-chapters')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    report = []
    for book in sorted((ROOT / 'tests/testBook').rglob('*.epub')):
        identity = digest(book)
        result = subprocess.run([str(args.binary.resolve()), str(book)], capture_output=True, text=True, timeout=300)
        (args.output / f'{len(report)}.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        chapters = [{'index': int(i), 'status': int(s), 'codepoints': int(c), 'images': int(m), 'path': p}
                    for i, s, c, m, p in re.findall(r'chapter index=(\d+) status=(\d+) codepoints=(\d+) images=(\d+) path=([^\n]+)', result.stdout)]
        summary = re.search(r'validation status=(\d+) chapters=(\d+) failed=(\d+) peak=(\d+) used=(\d+) live=(\d+) leases=(\d+)', result.stdout)
        entry = {'book': book.name, 'sha256': identity, 'chapters': chapters,
                 'failed_chapters': [c for c in chapters if c['status']], 'original_unchanged': digest(book) == identity,
                 'passed': result.returncode == 0 and bool(summary) and summary[5] == summary[6] == summary[7] == '0'}
        if summary:
            entry['pool_peak_bytes'] = int(summary[4])
        report.append(entry)
        print(f'{"PASS" if entry["passed"] else "FAIL"} chapters {book.name} total={len(chapters)} failed={len(entry["failed_chapters"])}', flush=True)
    (args.output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return int(not report or any(not e['passed'] or not e['original_unchanged'] for e in report))


if __name__ == '__main__':
    raise SystemExit(main())
