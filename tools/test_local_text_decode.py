"""用户TXT与四字体的真实窗口翻页像素对照。 / Compare real-window TXT paging pixels using supplied books and four fonts."""
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
    parser.add_argument('--baseline', type=pathlib.Path)
    parser.add_argument('--output', type=pathlib.Path, default=ROOT / 'build-dev/local-text-decode')
    args = parser.parse_args()
    books = sorted((ROOT / 'tests/testBook').rglob('*.txt'))
    fonts = sorted(p for p in (ROOT / 'tests/testFont').rglob('*') if p.is_file() and p.suffix.lower() == '.ttf')
    assert books and fonts
    identities = {p: digest(p) for p in books + fonts}
    args.output.mkdir(parents=True, exist_ok=True)
    report = {'passed': False, 'scope': 'Shared TXT decoding and real SDL paging using isolated supplied copies; not physical TF/panel timing', 'runs': []}
    target = args.output / 'report.json'
    target.write_text(json.dumps(report), encoding='utf8')
    with tempfile.TemporaryDirectory() as temporary:
        root = pathlib.Path(temporary)
        for bi, original in enumerate(books):
            library = root / f'books-{bi}'
            library.mkdir()
            shutil.copy2(original, library / original.name)
            for fi, original_font in enumerate(fonts):
                font = root / original_font.name
                shutil.copy2(original_font, font)
                def run(binary, label):
                    frames = root / f'frames-{bi}-{fi}-{label}'
                    frames.mkdir()
                    state = root / f'state-{bi}-{fi}-{label}'
                    env = dict(os.environ, SDL_VIDEODRIVER='dummy', PN_SIM_CLOCK='12:34', PN_SIM_FRAME_DIR=str(frames), PN_SIM_LIBRARY_SCRIPT='library,enter,wait:2500,next,next,previous,window-close')
                    result = subprocess.run([str(binary.resolve()), '--library', str(library), '--font', str(font), '--state-dir', str(state)], env=env, capture_output=True, text=True, timeout=120)
                    (args.output / f'{bi}-{fi}-{label}.log').write_text(result.stdout + result.stderr, encoding='utf8')
                    assert result.returncode == 0 and 'used=0 live=0' in result.stdout and 'opening end status=0' in result.stdout, (original.name, original_font.name, label, result.stdout[-2000:], result.stderr[-1000:])
                    positions = [int(x) for x in re.findall(r'library_(?:position|confirmed) offset=(\d+)', result.stdout)]
                    assert len(positions) == 4 and positions[0] < positions[1] < positions[2] and positions[1] == positions[3], positions
                    # 窗口退出先保存并返回书库；最后一帧不是正文。/ Closing the window saves and returns to the library, so the last frame is not reading content.
                    captured = sorted(frames.glob('frame-*.pgm'))[-5:-1]
                    assert len(captured) == 4
                    pixels = [p.read_bytes() for p in captured]
                    header = b'P5\n684 1216\n255\n'
                    assert all(raw.startswith(header) and len(raw) == len(header) + 684 * 1216 for raw in pixels)
                    for pi, raw in enumerate(pixels):
                        (args.output / f'{bi}-{fi}-{label}-{pi}.pgm').write_bytes(raw)
                    assert pixels[1] == pixels[3]
                    return positions, pixels
                old = run(args.baseline, 'before') if args.baseline else None
                positions, pixels = run(args.sim, 'after')
                if old:
                    assert old == (positions, pixels), (original.name, original_font.name)
                report['runs'].append({'book': original.name, 'font': original_font.name, 'positions': positions, 'reverse_pixels_match': True, 'baseline_full_frames_match': bool(old), 'frames': [digest(args.output / f'{bi}-{fi}-after-{i}.pgm') for i in range(4)], 'closed_pool_zero': True})
                print('PASS TXT decode', original.name, original_font.name, flush=True)
    report['originals_unchanged'] = all(digest(p) == identity for p, identity in identities.items())
    assert report['originals_unchanged']
    report['passed'] = True
    target.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf8')


if __name__ == '__main__':
    main()
