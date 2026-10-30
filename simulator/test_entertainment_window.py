"""娱乐入口的真实事件循环与退出回归。 / Entertainment entry through the real event loop and clean exit."""
import os
import pathlib
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory() as folder:
    root = pathlib.Path(folder)
    books = root / "books"
    books.mkdir()
    (books / "one.txt").write_text("A quiet reading break.\n" * 100, encoding="utf-8")
    state = root / "state"
    state.mkdir()

    def run(script, budget=6291456):
        env = dict(os.environ, SDL_VIDEODRIVER="dummy", PN_SIM_LIBRARY_SCRIPT="library," + script)
        result = subprocess.run([sys.argv[1], "--library", str(books), "--state-dir", str(state), "--budget", str(budget)], env=env, capture_output=True, text=True, timeout=120)
        assert result.returncode == 0 and "used=0 live=0" in result.stdout, (result.stdout, result.stderr)
        return result.stdout

    for budget in (6291456, 2097152):
        out = run("tap:460:190,tap:128:408,back,enter,back,tap:200:190,tap:460:190,tap:300:190,tap:460:190,tap:85:1150,library,tap:50:190,tap:100:400,back,quit", budget)
        assert out.count("entertainment page=1 status=0") >= 4 and out.count("sudoku screen=0 status=0") >= 2, out
        assert out.count("library_open name=one.txt") == 1, out
    # 卡间空隙和卡下留白不能误入任一游戏；三个卡片中心分别进入。/ Card gaps and lower whitespace cannot open games; each card center enters its own game.
    out = run("tap:460:190,tap:235:408,tap:449:408,tap:128:648,tap:342:648,tap:556:648,tap:128:888,tap:342:888,tap:556:888,window-close")
    assert not any(name + " screen=" in out for name in ("sudoku", "sokoban", "minesweeper")), out
    out = run("tap:460:190,tap:128:408,back,tap:342:408,back,tap:556:408,back,window-close")
    assert all(name + " screen=0 status=0" in out for name in ("sudoku", "sokoban", "minesweeper")), out
    # 空书库也能进入，内容手势不误开旧书或书籍菜单。/ Empty shelves can enter; content gestures never open old books or book menus.
    (books / "one.txt").unlink()
    out = run("tap:460:190,press:128:408,wait:700,release:128:408,back,library-next,library-previous,enter,back,tap:50:190,quit")
    assert "actions open" not in out and "library_open" not in out, out
    assert "sudoku screen=0 status=0" in out, out
print("entertainment: tabs, game selection/back, keyboard, empty library, 2 MiB budget and no leaks passed")
