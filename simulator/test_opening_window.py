"""开书反馈与连点/取消：真实SDL线程、加载画面与最终资源回收。 / Real SDL opening feedback, duplicate suppression, cancellation and cleanup."""
import os, pathlib, re, subprocess, sys, tempfile
with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    books = root / 'books'
    books.mkdir()
    (books / 'a.txt').write_text('原创开书反馈与阅读位置测试。\n' * 4000, encoding='utf-8')
    def run(name, script):
        frames = root / (name + '-frames')
        frames.mkdir()
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', PN_SIM_PRESENT_DELAY_MS='600',
                   PN_SIM_LIBRARY_SCRIPT=script, PN_SIM_FRAME_DIR=str(frames))
        result = subprocess.run([sys.argv[1], '--library', str(books), '--state-dir', str(root / name)],
                                env=env, text=True, capture_output=True, timeout=30)
        assert result.returncode == 0 and 'used=0 live=0' in result.stdout, (result.stdout, result.stderr)
        return result, frames
    opened, frames = run('duplicate', 'library,race-open-duplicate:200,wait:300,back,quit')
    assert opened.stdout.count('opening begin name=a.txt') == 1 and opened.stdout.count('library_open name=a.txt') == 1, opened.stdout
    assert 'opening end status=0' in opened.stdout, opened.stdout
    assert int(re.search(r'async mailbox coalesced=\d+ dropped=(\d+)', opened.stdout)[1]) >= 1, opened.stdout
    loading = 0
    for file in frames.glob('*.pgm'):
        pixels = file.read_bytes().split(b'\n', 3)[3]
        if all(value == 255 for value in pixels[684*1144:]) and any(value < 255 for value in pixels[684*460:684*510]):
            loading += 1
    assert loading == 1, loading
    cancelled, _ = run('cancel', 'library,race-open-home:200,wait:300,quit')
    assert 'opening end status=8' in cancelled.stdout and 'library_open name=a.txt' not in cancelled.stdout, (cancelled.stdout, cancelled.stderr)
    assert 'navigation root=home status=0' in cancelled.stdout, cancelled.stdout
print('opening: one visible loading frame, ignored duplicate, urgent cancellation and zero live allocations passed')
