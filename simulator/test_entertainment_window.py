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
        out = run("tap:460:190,tap:100:400,tap:300:1000,enter,back,tap:200:190,tap:460:190,tap:300:190,tap:460:190,tap:85:1150,library,tap:50:190,tap:100:400,back,quit", budget)
        assert out.count("entertainment page=1 status=0") >= 4 and out.count("entertainment page=2 status=0") >= 2, out
        assert out.count("library_open name=one.txt") == 1, out
    # 空书库也能进入，内容手势不误开旧书或书籍菜单。/ Empty shelves can enter; content gestures never open old books or book menus.
    (books / "one.txt").unlink()
    out = run("tap:460:190,press:100:400,wait:700,release:100:400,tap:300:1000,library-next,library-previous,enter,back,tap:50:190,quit")
    assert "actions open" not in out and "library_open" not in out, out
    assert "entertainment page=2 status=0" in out, out
print("entertainment: tabs, intro/back, keyboard, empty library, 2 MiB budget and no leaks passed")
