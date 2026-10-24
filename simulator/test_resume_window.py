"""真实SDL循环的跨进程续读；主机像素与计时不代替面板。 / Cross-process resume through the real SDL loop; host pixels/timing never substitute for panel measurements."""
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tests' / 'host'))
from test_resume import fixture


def main():
    exe, font = sys.argv[1:3]
    baseline = sys.argv[3] if len(sys.argv) > 3 else exe
    # 可指定持久证据目录；窗口驱动由外层选择，默认只用于自动回归。/ Allow persistent evidence; the caller chooses the video driver, defaulting to automated regression.
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(os.environ.get('PN_RESUME_EVIDENCE', tmp))
        root.mkdir(parents=True, exist_ok=True)
        copied_font = root / 'font.ttf'
        shutil.copyfile(font, copied_font)
        font = str(copied_font)
        fixtures = root / 'fixtures'
        fixtures.mkdir()
        fixture(fixtures)
        for kind in ('txt', 'epub'):
            books = root / (kind + '-books')
            books.mkdir()
            shutil.copy2(fixtures / ('book.' + kind), books)
            seed = root / (kind + '-seed')

            def run(binary, label, state, script):
                capture = root / label
                capture.mkdir()
                env = dict(os.environ, SDL_VIDEODRIVER=os.environ.get('SDL_VIDEODRIVER', 'dummy'), PN_SIM_LIBRARY_SCRIPT='library,' + script,
                           PN_SIM_FRAME_DIR=str(capture), PN_SIM_CLOCK='12:34', PN_SIM_TIMING='1', PN_READING_CACHE=str(state / 'reading'))
                result = subprocess.run([binary, '--library', str(books), '--font', font, '--state-dir', str(state)], env=env, capture_output=True, text=True, timeout=150)
                (capture / 'stdout.log').write_text(result.stdout)
                (capture / 'stderr.log').write_text(result.stderr)
                assert result.returncode == 0 and 'used=0 live=0' in result.stdout, (label, result.stdout, result.stderr[-3000:])
                frames = [p.read_bytes() for p in sorted(capture.glob('frame-*.pgm'))]
                return result, frames

            first, frames = run(exe, kind + '-seed-frames', seed, 'enter,next,next,wait:6000,back,quit')
            assert len(list((seed / 'reading' / 'resume').glob('*.pnr'))) == 2, (kind, first.stdout, first.stderr)
            results = {}
            for label, binary in [('before', baseline), ('after', exe), ('missing', exe)]:
                state = root / (kind + '-' + label)
                shutil.copytree(seed, state)
                if label == 'missing':
                    shutil.rmtree(state / 'reading' / 'resume')
                result, frames = run(binary, kind + '-' + label + '-frames', state, 'enter,next,prev,back,quit')
                results[label] = frames[-4:-1]
                measures = [(phase, int(us)) for phase, us in re.findall(r'^profile phase=(sim\.\S+|resume\.hit) elapsed_us=(\d+)', result.stderr, re.M)]
                print(kind, label, json.dumps(measures), flush=True)
                if label == 'after':
                    assert 'phase=resume.hit' in result.stderr, (kind, result.stderr)
            assert results['before'] == results['after'] == results['missing'], (kind, 'full SDL frame mismatch')
            print(kind, 'SDL restart, first NEXT, previous and missing-cache full pixels identical', flush=True)


if __name__ == '__main__':
    main()
