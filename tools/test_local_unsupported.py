"""核查用户未支持格式的实际入口行为，不能把拒绝计为阅读支持。 / Check actual entry behavior for unsupported user formats without counting rejection as reading support."""
import argparse
import json
import os
import pathlib
import re
import shutil
import subprocess
import tempfile
from test_local_samples import ROOT, digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sim', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, default=ROOT / 'build-dev/local-unsupported')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    report = []
    with tempfile.TemporaryDirectory() as temporary:
        root = pathlib.Path(temporary)
        for book in sorted((ROOT / 'tests/testBook').rglob('*')):
            if not book.is_file() or book.suffix.lower() in ('.epub', '.txt'):
                continue
            identity = digest(book)
            library = root / f'books-{len(report)}'
            library.mkdir()
            shutil.copy2(book, library / book.name)
            result = subprocess.run([str(args.sim.resolve()), '--library', str(library), '--state-dir', str(root / f'state-{len(report)}')],
                env=dict(os.environ, SDL_VIDEODRIVER='dummy', PN_SIM_TIMING='1', PN_SIM_LIBRARY_SCRIPT='library,enter,window-close'), capture_output=True, text=True, timeout=60)
            log = result.stdout + result.stderr
            (args.output / f'{len(report)}.log').write_text(log, encoding='utf-8')
            opened = re.search(r'(?:library_open prepare .*? |shelf_open_failed )status=(\d+)', log)
            if not opened:
                opened = re.search(r'opening end status=(\d+)', log)
            counts = re.findall(r'library_page .*?count=(\d+)', log)
            entry = {'book': book.name, 'format': book.suffix.lower(), 'reading_supported': False,
                     'visible_in_catalog': bool(counts and int(counts[-1])), 'open_status': int(opened[1]) if opened else None,
                     'clean_exit': result.returncode == 0 and 'used=0 live=0' in log, 'original_unchanged': digest(book) == identity}
            report.append(entry)
            print(json.dumps(entry, ensure_ascii=False), flush=True)
    (args.output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return int(any(not e['clean_exit'] or not e['original_unchanged'] or e['open_status'] not in (None, 3, 9) for e in report))


if __name__ == '__main__':
    raise SystemExit(main())
