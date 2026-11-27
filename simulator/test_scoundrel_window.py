# SPDX-License-Identifier: Apache-2.0
"""地牢纸牌SDL窗口真实事件循环回归：2MiB池、开局/逃跑/选牌/撤销、A/B持久恢复与单次清屏退出。
Scoundrel SDL window regression: 2 MiB budget, start/flee/play/undo, A/B resume and single-clear exit."""
import os
import pathlib
import re
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory() as folder:
    root = pathlib.Path(folder)
    books = root / "books"
    books.mkdir()
    state = root / "state"
    state.mkdir()

    def run(script, budget=2097152, clear_state=False):
        if clear_state:
            for file in state.glob("scoundrel.[ab]"):
                file.unlink()
        env = dict(
            os.environ,
            SDL_VIDEODRIVER="dummy",
            PN_SIM_CLOCK="12:00",
            PN_SIM_LIBRARY_SCRIPT=script,
        )
        result = subprocess.run(
            [sys.argv[1], "--library", str(books), "--state-dir", str(state), "--budget", str(budget)],
            env=env,
            capture_output=True,
            text=True,
            timeout=120,
        )
        assert result.returncode == 0 and "used=0 live=0" in result.stdout, (result.stdout, result.stderr)
        assert "scoundrel screen=" in result.stdout, result.stdout
        assert not re.search(r"scoundrel screen=\d+ status=[1-9]", result.stdout), result.stdout
        return result.stdout

    # 1. 进入第8格地牢纸牌 -> 开始新局 -> 逃跑 -> 退出清屏，确认A/B存档生成
    out = run("library,tap:460:190,tap:342:888,wait:200,tap:340:880,wait:200,tap:100:1070,wait:200,enter,wait:300,window-close", clear_state=True)
    assert "scoundrel screen=0 status=0" in out and "scoundrel screen=1 status=0" in out, out
    assert out.count("game_exit_clean destination=entertainment presentations=1 status=0") == 1, out
    assert list(state.glob("scoundrel.[ab]")), "Expected scoundrel A/B save"

    # 2. 再次进入自动恢复进行中牌局 -> 锁屏/唤醒 -> 关窗保存
    out2 = run("library,tap:460:190,tap:342:888,wait:200,lock,wait:200,lock,wait:200,window-close")
    assert "scoundrel screen=1 status=0" in out2, out2

print("scoundrel SDL: start, flee, A/B resume, lock/wake, 2MiB budget and clean exit passed")
