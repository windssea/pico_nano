"""用户书籍/字体的真实设备锁屏路由软件回归。 / Software regression of device lock routing with supplied books/fonts."""
import argparse
import json
import pathlib
import re
import shutil
import subprocess
import tempfile
from test_local_samples import ROOT, digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, default=ROOT / 'build-dev/local-lock')
    args = parser.parse_args()
    books = sorted(p for p in (ROOT / 'tests/testBook').rglob('*') if p.is_file() and p.suffix.lower() in ('.epub', '.txt'))
    fonts = sorted(p for p in (ROOT / 'tests/testFont').rglob('*') if p.is_file() and p.suffix.lower() == '.ttf')
    if not books or not fonts:
        raise ValueError('Requires supplied books and fonts')
    identities = {p: digest(p) for p in books + fonts}
    args.output.mkdir(parents=True, exist_ok=True)
    report = {'passed': False, 'runs': [], 'scope': 'Real device product routing with hardware ports replaced; not physical sleep/wake or power validation'}
    target = args.output / 'report.json'
    target.write_text(json.dumps(report), encoding='utf-8')
    with tempfile.TemporaryDirectory() as temporary:
        root = pathlib.Path(temporary)
        for index, original in enumerate(books):
            local_book = root / f'book-{index}{original.suffix}'
            shutil.copy2(original, local_book)
            for font_index, original_font in enumerate(fonts):
                local_font = root / f'font-{font_index}.ttf'
                if not local_font.exists():
                    shutil.copy2(original_font, local_font)
                state = root / f'state-{index}-{font_index}'
                state.mkdir()
                result = subprocess.run([str(args.binary.resolve()), original.suffix[1:].lower(), str(local_book), str(local_font), str(state)],
                                        capture_output=True, text=True, timeout=180)
                (args.output / f'{index}-{font_index}.log').write_text(result.stdout + result.stderr, encoding='utf-8')
                match = re.search(r'device_lock kind=(epub|txt) cycles=3 pixels_exact=1 position_exact=1 restore_failure_retry=1 stale_power_drained=1 used=0 live=0 peak=(\d+)', result.stdout)
                assert result.returncode == 0 and match, (original.name, original_font.name, result.returncode, result.stdout[-1000:], result.stderr[-2500:])
                report['runs'].append({'book': original.name, 'font': original_font.name, 'cycles': 3, 'exact_pixels_and_position': True,
                                       'restore_failure_retry': True, 'stale_power_drained': True, 'closed_pool_zero': True, 'pool_peak': int(match[2])})
                target.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
                print(f'PASS lock {original.name} / {original_font.name} peak={match[2]}', flush=True)
    report['originals_unchanged'] = all(digest(p) == identity for p, identity in identities.items())
    assert report['originals_unchanged']
    report['passed'] = True
    target.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
