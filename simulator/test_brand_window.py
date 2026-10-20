"""开机插画与默认锁屏分别核验，版本号只叠加到开机帧。 / Verify boot art and default lock separately; overlay the version only on boot."""
import os
import json
import hashlib
import pathlib
import subprocess
import sys
import tempfile
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
from build_firmware_art import resize

# 产品画面描述实体按键，快捷键只属于模拟器控制。 / Product hints describe hardware keys; shortcuts belong to simulator controls.
source = pathlib.Path(__file__).with_name('library_sdl.c').read_text(encoding='utf-8')
assert '"按电源键继续阅读"' not in source
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
    # 开机版本号只占固定框；锁屏使用相同熊猫图。/ The version occupies a fixed box; the lock retains the original brand bounds.
    version_box = lambda x, y: 540 <= x < 660 and 1171 <= y < 1192  # 宽度随版本号变化 / width varies with the version
    every = [(i % 684, i // 684) for i, value in enumerate(boot) if value < 255]
    assert sum(1 for x, y in every if version_box(x, y)) > 100
    expected = resize(pathlib.Path(__file__).resolve().parents[1] / 'assets/image/水墨熊猫阅读时光.png', 684, 1216, False)
    assert all(value == expected[i] * 17 for i, value in enumerate(boot) if not version_box(i % 684, i // 684))
    assert lock == bytes(v*17 for v in expected)
    assert len(set(boot)) > 2 and any(value < 255 for value in lock[684 * 1104:])
print('PicoNano: separate native boot art/default panda, exact boot pixels, version label and no lock overlay passed')
