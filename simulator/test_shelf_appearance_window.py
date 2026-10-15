"""书架密度/样式保存、十二本选书与分页回归。 / Shelf density/style persistence, twelve-book selection and pagination regression."""
import os
import pathlib
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory() as folder:
    root = pathlib.Path(folder)
    books = root / "books"
    books.mkdir()
    for i in range(18):
        (books / f"book{i:02}.txt").write_text("Read Pico remembers this book.\n" * 60, encoding="utf8")
    state = root / "state"
    state.mkdir()

    def run(script, capture=None):
        env = dict(os.environ, SDL_VIDEODRIVER="dummy", PN_SIM_LIBRARY_SCRIPT="library,"+script)
        if capture is not None:
            env["PN_SIM_FRAME_DIR"] = str(capture)
        result = subprocess.run(
            [sys.argv[1], "--library", str(books), "--state-dir", str(state)],
            env=env,
            capture_output=True, text=True, timeout=120,
        )
        assert result.returncode == 0 and "used=0 live=0" in result.stdout, (result.stdout, result.stderr)
        return result.stdout

    # 保存四列拟物，退出设置后立即重分页。/ Save four-column skeuomorphism and re-page on leaving settings.
    out = run("settings,tap:100:450,tap:550:240,tap:100:780,back,back,tap:580:650,back,library-next,library-previous,quit")
    assert "columns=4 style=2" in out and "library_open name=book07.txt" in out, out
    assert "library_page first=book12.txt" in out and "library_page first=book00.txt" in out, out
    assert (state / "shelf.a").is_file() or (state / "shelf.b").is_file()
    # 重启仍为四列，第7/8项和分页的命令不冲突。/ Four columns survive restart; books seven/eight do not collide with paging commands.
    out = run("tap:420:650,back,tap:580:650,back,library-next,library-next,library-previous,quit")
    assert "library_open name=book06.txt" in out and "library_open name=book07.txt" in out, out
    assert "library_page first=book12.txt" in out and "library_page first=book12.txt" in out, out
    # 切回三列纸页，同时检查四列书籍菜单与列表仍使用五项分页。/ Return to three-column paper; also verify the fourth-column menu and five-item list paging.
    out = run("press:600:650,wait:800,release:600:650,back,settings,tap:100:450,tap:100:240,tap:550:780,back,back,library-next,tap:600:190,library-next,quit")
    assert "actions open index=7" in out and "columns=3 style=3" in out, out
    assert "library_page first=book09.txt" in out and "library_page first=book05.txt" in out, out
    out = run("library-next,quit")
    assert "library_page first=book09.txt" in out, out
    # 初始无焦点；主动键盘导航才出现书名下划线，返回书架后恢复。/ No initial focus; deliberate keyboard navigation adds a caption underline and returning clears it.
    capture = root / "focus-frames"
    capture.mkdir()
    out = run("toc-down,toc-down,enter,back,quit", capture)
    assert "library_open name=book01.txt" in out, out
    frames = [p.read_bytes().split(b"\n", 3)[3] for p in sorted(capture.glob("*.pgm"))]
    assert len(frames) >= 5
    def at(frame, x, y):
        return frame[y * 684 + x]
    assert at(frames[1], 80, 515) == 255 and at(frames[1], 302, 515) == 255
    assert at(frames[2], 80, 515) < 255 and at(frames[2], 302, 515) == 255
    assert at(frames[3], 80, 515) == 255 and at(frames[3], 302, 515) < 255
    assert at(frames[-1], 80, 515) == 255 and at(frames[-1], 302, 515) == 255


print("shelf appearance: persistence, twelve book identities, menus, bidirectional paging and list density passed")
