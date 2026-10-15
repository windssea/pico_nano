"""现有公开边界文本的真实首页准备/冷开像素对照；非用户整库或面板计时。 / Actual Home preparation versus cold opening with an existing public boundary text, not user-corpus or panel timing."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

fixture = Path(__file__).resolve().parents[1]/'tests/fixtures/reader_sample.txt'
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory); books = root/'books'; books.mkdir(); shutil.copyfile(fixture, books/'reader.txt')

    def run(name, script, seed=None):
        state = root/(name+'-state')
        if seed: shutil.copytree(seed, state)
        else: state.mkdir()
        cache = root/(name+'-cache'); cache.mkdir(); frames = root/(name+'-frames'); frames.mkdir()
        env = {**os.environ, 'SDL_VIDEODRIVER': 'dummy', 'PN_SIM_CLOCK': '12:34', 'PN_SIM_FRAME_DIR': str(frames), 'PN_SIM_LIBRARY_SCRIPT': script}
        result = subprocess.run([sys.argv[1], '--library', str(books), '--state-dir', str(state), '--cover-cache', str(cache)], env=env, capture_output=True, text=True, timeout=30)
        assert result.returncode == 0 and 'used=0 live=0' in result.stdout, (name, result.stdout, result.stderr)
        return result.stdout, sorted(frames.glob('frame-*.pgm'))[-1].read_bytes(), state

    _, _, seed = run('seed', 'library,enter,close-window')
    cold, cold_pixels, _ = run('cold', 'continue,close-window', seed)
    warm, warm_pixels, _ = run('warm', 'wait:2500,continue,close-window', seed)
    assert 'book_prepare status=0 ready=1 preparations=1' in warm and warm.count('library_prepared take=1') == 1, warm
    assert 'library_prepared' not in cold and 'book_prepare' not in cold, cold
    assert warm_pixels == cold_pixels
    closed, _, _ = run('closed', 'wait:2500,settings,close-window', seed)
    assert 'book_prepare status=0 ready=1' in closed and 'library_prepared' not in closed and 'library_open name=' not in closed, closed
print('Home idle preparation, exact cold-open frame, settings release and pool zero passed; no panel timing claim')
