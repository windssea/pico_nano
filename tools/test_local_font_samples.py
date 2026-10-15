"""用户真实字库的列表首帧/逐轮小样与旧版完整像素对照。 / Compare first paint, idle font tiles and full pixels using supplied fonts."""
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
    parser.add_argument('--baseline', type=pathlib.Path)
    parser.add_argument('--output', type=pathlib.Path, default=ROOT / 'build-dev/local-font-samples')
    args = parser.parse_args()
    fonts = sorted(p for p in (ROOT / 'tests/testFont').rglob('*') if p.is_file() and p.suffix.lower() == '.ttf')
    if not fonts:
        raise ValueError('Requires supplied tests/testFont fonts')
    identities = {p: digest(p) for p in fonts}
    args.output.mkdir(parents=True, exist_ok=True)
    report = {'passed': False, 'cases': [], 'scope': 'Host file opens/pixels, not panel or TF timings'}
    target = args.output / 'report.json'
    target.write_text(json.dumps(report), encoding='utf-8')
    with tempfile.TemporaryDirectory() as temporary:
        root = pathlib.Path(temporary)
        for i, selected in enumerate([[font] for font in fonts] + [fonts]):
            folder = root / str(i)
            folder.mkdir()
            for font in selected:
                shutil.copy2(font, folder / font.name)
            images = []
            stats = []
            programs = ([('old', args.baseline)] if args.baseline else []) + [('new', args.binary)]
            for label, binary in programs:
                image = root / f'{i}-{label}.pgm'
                result = subprocess.run([str(binary.resolve()), str(folder), str(root), str(root), str(image)],
                                        capture_output=True, text=True, timeout=120)
                (args.output / f'{i}-{label}.log').write_text(result.stdout + result.stderr, encoding='utf-8')
                match = re.search(r'font samples first_opens=(\d+) total_opens=(\d+) max_step_opens=(\d+) steps=(\d+) presents=(\d+) peak=(\d+) used=0 live=0', result.stdout)
                assert result.returncode == 0 and match and image.exists(), (label, selected, result.stderr)
                stats.append(dict(zip(('first_opens', 'total_opens', 'max_step_opens', 'steps', 'presents', 'peak_bytes'), map(int, match.groups()))))
                images.append(image.read_bytes())
            count = min(len(selected), 6)
            after = stats[-1]
            assert after['first_opens'] == 0 and after['total_opens'] == count and after['max_step_opens'] == 1
            if args.baseline:
                assert images[0] == images[1] and stats[0]['first_opens'] == stats[0]['total_opens'] == count
                assert after['presents'] == stats[0]['presents'] + 1
            report['cases'].append({'fonts': [p.name for p in selected], 'before': stats[0] if args.baseline else None, 'after': after,
                                    'baseline_pixels_equal': True if args.baseline else None, 'source_independent_repaint': True})
            print(f'PASS font samples {i}: first opens {count}->0; one face per idle step; exact final pixels', flush=True)
    report['originals_unchanged'] = all(digest(p) == value for p, value in identities.items())
    assert report['originals_unchanged']
    report['passed'] = True
    target.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
