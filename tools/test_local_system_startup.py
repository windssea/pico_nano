"""用户原书/字体副本：已加载系统源后的开书；固定文件端口计数不代表TF/面板性能。 / User copies: startup after loading the system source; immutable-port counts are not hardware timing."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from test_local_samples import ROOT, digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--expect-reuse', action='store_true')
    parser.add_argument('--warm-prepare', action='store_true', help='Compare a never-displayed idle preparation with a cold single-hash baseline on the immutable port')
    parser.add_argument('--baseline-warm', action='store_true', help='Use idle preparation in the independent baseline too')
    parser.add_argument('--expect-layout', action='store_true', help='Require first-page layout in idle and no repeated layout on click')
    parser.add_argument('--limit', type=int, help='Development subset only; omit for the full user library')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    books = sorted(p for p in (ROOT/'tests/testBook').rglob('*') if p.is_file() and p.suffix.lower() in ('.epub', '.txt'))
    fonts = sorted(p for p in (ROOT/'tests/testFont').rglob('*') if p.is_file() and p.suffix.lower() == '.ttf')
    assert books and fonts
    originals = {p: digest(p) for p in books+fonts}
    selected = books[:args.limit] if args.limit else books
    if args.warm_prepare and not args.expect_reuse: parser.error('--warm-prepare requires --expect-reuse')
    if (args.baseline_warm or args.expect_layout) and not args.warm_prepare: parser.error('layout comparisons require --warm-prepare')
    if args.baseline_warm and not args.baseline: parser.error('--baseline-warm requires an independent --baseline')
    if args.warm_prepare and not args.baseline: args.baseline = args.binary
    report = {'passed': False, 'scope': __doc__, 'subset': bool(args.limit), 'expect_reuse': args.expect_reuse, 'warm_prepare': args.warm_prepare,
              'baseline_warm': args.baseline_warm, 'expect_layout': args.expect_layout,
              'binary_sha256': digest(args.binary), 'baseline_sha256': digest(args.baseline) if args.baseline else None, 'runs': []}
    report_path = args.output/'report.json'
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        installed = root/'fonts'; installed.mkdir()
        for fi, font in enumerate(fonts):
            copied_font = installed/f'{fi}.ttf'; shutil.copy2(font, copied_font)
            for bi, book in enumerate(selected):
                copied_book = root/f'book-{bi}{book.suffix}'; shutil.copy2(book, copied_book)
                for primary in (False, True):
                    case = f'{fi}-{bi}-'+('primary' if primary else 'fallback')

                    def run(binary, label):
                        state = root/f'{case}-{label}'; state.mkdir()
                        frames = args.output.resolve()/f'{case}-{label}'; frames.mkdir()
                        env = {**os.environ, 'PN_FONT_STARTUP_SYSTEM': '1', 'PN_FONT_STARTUP_FRAMES': str(frames)}
                        env.pop('PN_FONT_STARTUP_FAULT', None); env.pop('PN_FONT_STARTUP_PRIMARY', None)
                        if primary: env['PN_FONT_STARTUP_PRIMARY'] = '1'
                        env.pop('PN_FONT_STARTUP_WARM', None)
                        if args.warm_prepare and (label == 'after' or args.baseline_warm): env['PN_FONT_STARTUP_WARM'] = '1'
                        result = subprocess.run([str(binary.resolve()), str(copied_book), str(copied_font), str(state)], capture_output=True, text=True, env=env, timeout=180)
                        (args.output/f'{case}-{label}.log').write_text(result.stdout+result.stderr, encoding='utf8')
                        assert result.returncode == 0, (case, label, result.stdout[-2000:], result.stderr[-2000:])
                        line = next(line for line in result.stdout.splitlines() if line.startswith('startup '))
                        metrics = {k: int(v) for k, v in (word.split('=') for word in line.split()[1:])}
                        assert metrics['status'] == metrics['used'] == metrics['live'] == 0
                        warm = args.warm_prepare and (label == 'after' or args.baseline_warm)
                        assert metrics['book_hashes'] == metrics['book_opens'] == (0 if warm else 1) and metrics['font_get_hashes'] == 0
                        idle = next((line for line in result.stdout.splitlines() if line.startswith('idle_prepare ')), None)
                        if warm:
                            assert idle
                            prepared = {k: int(v) for k, v in (word.split('=') for word in idle.split()[1:])}
                            assert prepared['book_hashes'] == 1 and prepared['state_writes'] == 0
                        pixels = sorted(frames.glob('frame-*.raw'))
                        assert len(pixels) == metrics['presents'] == 3 and all(p.stat().st_size == 415872 for p in pixels)
                        turn = next(line for line in result.stdout.splitlines() if line.startswith('turn_io '))
                        layout = {line.split()[0]: int(line.split('calls=')[1]) for line in result.stdout.splitlines() if line.startswith(('idle_layout calls=', 'click_layout calls='))}
                        if args.expect_layout and label == 'after': assert layout == {'idle_layout': 1, 'click_layout': 0}, (case, layout)
                        return {'metrics': metrics, 'idle_prepare': idle, 'layout': layout, 'turn_io': turn, 'frames': [hashlib.sha256(p.read_bytes()).hexdigest() for p in pixels]}, pixels

                    before, old_frames = run(args.baseline, 'before') if args.baseline else (None, None)
                    after, frames = run(args.binary, 'after')
                    expected = 0 if args.expect_reuse else 1
                    assert after['metrics']['full_hashes'] == expected and after['metrics']['font_opens'] == (0 if args.warm_prepare else 1), after
                    if before:
                        assert before['metrics']['full_hashes'] == (0 if args.warm_prepare else 1) and before['metrics']['font_opens'] == (0 if args.baseline_warm else 1)
                        assert before['frames'] == after['frames'] and len(old_frames) == len(frames)
                        assert all(a.read_bytes() == b.read_bytes() for a, b in zip(old_frames, frames))
                        if args.warm_prepare:
                            assert before['metrics']['book_read_bytes'] - after['metrics']['book_read_bytes'] >= (0 if args.baseline_warm else book.stat().st_size)
                            assert after['metrics']['font_read_bytes'] <= before['metrics']['font_read_bytes']
                        elif args.expect_reuse:
                            assert before['metrics']['font_read_bytes'] - after['metrics']['font_read_bytes'] == font.stat().st_size
                        else:
                            assert before['metrics']['font_read_bytes'] == after['metrics']['font_read_bytes']
                        if not args.warm_prepare: assert before['turn_io'] == after['turn_io'] and after['metrics']['peak'] <= before['metrics']['peak']
                        for key in ('book_reads', 'book_read_bytes', 'book_seeks'):
                            assert after['metrics'][key] <= before['metrics'][key] if args.warm_prepare else before['metrics'][key] == after['metrics'][key], (case, key)
                    report['runs'].append({'book': book.name, 'font': font.name, 'primary': primary, 'before': before, 'after': after})
                    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
                    print('PASS system startup', case, book.name, font.name, flush=True)
    assert all(digest(p) == value for p, value in originals.items())
    report['originals_unchanged'] = report['passed'] = True
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf8')


if __name__ == '__main__':
    main()
