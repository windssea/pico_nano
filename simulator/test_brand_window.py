"""开机图/默认锁屏共用logo，提示带独立，原生帧无品牌图裁切。 / Boot/default lock share the logo, keep the hint separate and preserve native artwork bounds."""
import os
import pathlib
import subprocess
import sys
import tempfile

# 产品画面描述实体按键，快捷键只属于模拟器控制。 / Product hints describe hardware keys; shortcuts belong to simulator controls.
source = pathlib.Path(__file__).with_name('library_sdl.c').read_text(encoding='utf-8')
assert '"按电源键继续阅读"' in source
assert '按 L 或回车' not in source

with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    books = root / 'books'
    books.mkdir()
    (books / 'a.txt').write_text('原始书籍。\n' * 10, encoding='utf-8')
    frames = root / 'frames'
    frames.mkdir()
    result = subprocess.run([sys.argv[1], '--library', str(books), '--state-dir', str(root / 'state')],
                            env=dict(os.environ, SDL_VIDEODRIVER='dummy', PN_SIM_BOOT_PREVIEW='1',
                                     PN_SIM_FRAME_DIR=str(frames), PN_SIM_LIBRARY_SCRIPT='lock,window-close'),
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == 0 and 'used=0 live=0' in result.stdout, (result.stdout, result.stderr)
    captures = sorted(frames.glob('*.pgm'))
    assert len(captures) == 3, result.stdout
    def pixels(path):
        fields = path.read_bytes().split(b'\n', 3)
        assert fields[:3] == [b'P5', b'684 1216', b'255']
        assert len(fields[3]) == 684 * 1216
        return fields[3]
    boot, lock = pixels(captures[0]), pixels(captures[-1])
    assert boot[:684 * 1104] == lock[:684 * 1104]
    ink = [(i % 684, i // 684) for i, value in enumerate(boot) if value < 255]
    assert ink and min(x for x, y in ink) >= 62 and max(x for x, y in ink) < 622
    assert min(y for x, y in ink) > 400 and max(y for x, y in ink) < 800
    assert len(set(boot)) > 2 and any(value < 255 for value in lock[684 * 1104:])
print('PicoNano: shared native boot/default logo, white transparency, safe bounds and independent hint passed')
