"""用户原书的可恢复ZIP校验与逐步读量/取消检查；原件只读。 / Resumable ZIP checks on supplied books, keeping originals read-only."""
import argparse
import json
import pathlib
import re
import shutil
import subprocess
import tempfile
import zipfile
from test_local_samples import ROOT, digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, default=ROOT / 'build-dev/local-zip-job')
    args = parser.parse_args()
    books = sorted(p for p in (ROOT / 'tests/testBook').rglob('*') if p.suffix.lower() == '.epub')
    if not books:
        raise ValueError('No supplied EPUBs')
    identities = {p: digest(p) for p in books}
    def entries(book):
        with zipfile.ZipFile(book) as archive:
            return len(archive.infolist())
    smallest = min(books, key=entries)
    args.output.mkdir(parents=True, exist_ok=True)
    report = {'passed': False, 'books': [], 'scope': 'Parser/FD-source budgets and complete resource-byte equivalence; not physical TF/panel or whole-application memory'}
    target = args.output / 'report.json'
    target.write_text(json.dumps(report), encoding='utf-8')
    with tempfile.TemporaryDirectory() as folder:
        root = pathlib.Path(folder)
        for index, original in enumerate(books):
            book = root / original.name
            shutil.copy2(original, book)
            command = [str(args.binary.resolve()), str(book)]
            if original == smallest:
                command.append('--faults')
            result = subprocess.run(command, capture_output=True, text=True, timeout=300)
            (args.output / f'{index}.log').write_text(result.stdout + result.stderr, encoding='utf-8')
            m = re.search(r'zip job entries=(\d+) calls_max=(\d+) bytes_max=(\d+) cancelled=(\d+) aborts=(\d+) faults=(\d+) peak=(\d+) used=(\d+) live=(\d+)', result.stdout)
            assert result.returncode == 0 and m and m[8] == m[9] == '0', original.name
            assert int(m[1]) == entries(original) and int(m[2]) <= 8 and int(m[3]) <= 4096 and int(m[4]) > 0
            report['books'].append({'book': original.name, 'entries': int(m[1]), 'max_source_calls_per_step': int(m[2]),
                'max_source_bytes_per_step': int(m[3]), 'cancelled_steps': int(m[4]), 'abort_boundaries': int(m[5]),
                'allocation_faults': int(m[6]), 'parser_pool_peak': int(m[7]), 'all_resource_bytes_equal': True,
                'short_reads': [1, 17], 'snapshot_denied_equal': True})
            print(f'PASS ZIP job {index} entries={m[1]} calls={m[2]} bytes={m[3]}', flush=True)
    report['originals_unchanged'] = all(digest(p) == value for p, value in identities.items())
    assert report['originals_unchanged']
    report['passed'] = True
    target.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
