"""真实用户书库的阅读中间键、排版保存与首页返回旅程。 / Supplied-book journeys for middle keys, saved styles and Home."""
import argparse
import json
import os
import pathlib
import shutil
import subprocess
import tempfile
from test_local_samples import ROOT, digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sim', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, default=ROOT / 'build-dev/local-middle-key')
    args = parser.parse_args()
    books = sorted(p for p in (ROOT / 'tests/testBook').rglob('*') if p.is_file() and p.suffix.lower() in ('.epub', '.txt'))
    font = ROOT / 'tests/testFont/霞鹜文楷.ttf'
    if not books or not font.is_file():
        raise ValueError('Requires supplied books and 霞鹜文楷.ttf')
    identities = {p: digest(p) for p in books + [font]}
    args.output.mkdir(parents=True, exist_ok=True)
    report = {'passed': False, 'books': [], 'scope': 'Host shared UI/input journey; physical keys/panel need hardware validation'}
    target = args.output / 'report.json'
    target.write_text(json.dumps(report), encoding='utf-8')
    with tempfile.TemporaryDirectory() as temporary:
        root = pathlib.Path(temporary)
        local_font = root / font.name
        shutil.copy2(font, local_font)
        for i, original in enumerate(books):
            folder = root / str(i)
            folder.mkdir()
            shutil.copy2(original, folder / original.name)
            script = 'library,enter,wait:2500,next,wait:500,key-middle-long,wait:200,key-home,wait:200,styles,style-plus,key-home,wait:200,key-home,wait:300,quit'
            env = dict(os.environ, SDL_VIDEODRIVER='dummy', PN_SIM_CLOCK='12:34', PN_SIM_LIBRARY_SCRIPT=script)
            result = subprocess.run([str(args.sim.resolve()), '--library', str(folder), '--font', str(local_font), '--state-dir', str(root / f'state-{i}')],
                                    env=env, capture_output=True, text=True, timeout=120)
            (args.output / f'{i}.log').write_text(result.stdout + result.stderr, encoding='utf-8')
            required = ['opening end status=0', 'reading middle long settings status=0', 'reading settings middle status=0 active=0',
                        'style_ui command=2 status=0 active=0', 'reading middle home status=0', 'used=0 live=0']
            assert result.returncode == 0 and all(text in result.stdout for text in required), (original.name, result.returncode, result.stdout[-3000:], result.stderr)
            assert 'management home status=0' not in result.stdout, original.name
            report['books'].append({'book': original.name, 'opened': True, 'long_settings': True, 'short_settings_resumes': True,
                                    'style_saved': True, 'short_body_home': True, 'closed_pool_zero': True})
            print(f'PASS middle key {original.name}', flush=True)
    report['originals_unchanged'] = all(digest(p) == value for p, value in identities.items())
    assert report['originals_unchanged']
    report['passed'] = True
    target.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
