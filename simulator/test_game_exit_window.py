# SPDX-License-Identifier: Apache-2.0
"""游戏返回只消费一次清屏意图；PC不测面板残影。 / Game return consumes one clear intent; PC does not measure panel ghosting."""
import os
import pathlib
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory() as folder:
    root = pathlib.Path(folder)
    for name, entry_y, start_y in (("sudoku", 408, 1040), ("sokoban", 648, 952), ("minesweeper", 888, 980)):
        books, state = root / name / "books", root / name / "state"
        books.mkdir(parents=True)
        state.mkdir()
        enter = f"library,tap:460:190,tap:342:{entry_y},wait:400,"

        def run(script, expected):
            env = dict(os.environ, SDL_VIDEODRIVER="dummy", PN_SIM_CLOCK="12:00", PN_SIM_LIBRARY_SCRIPT=script)
            result = subprocess.run([sys.argv[1], "--library", str(books), "--state-dir", str(state), "--budget", "2097152"], env=env, capture_output=True, text=True, timeout=90)
            assert result.returncode == 0 and "used=0 live=0" in result.stdout, (result.stdout, result.stderr)
            assert f"{name} screen=" in result.stdout, result.stdout
            assert result.stdout.count("game_exit_clean destination=entertainment presentations=1 status=0") == expected, result.stdout
            if expected:
                assert result.stdout.count("entertainment page=1 status=0") == 2, result.stdout
            return result.stdout

        # 选择页返回与游戏中间返回都只呈现一次目标；关窗口只是保存，不返回娱乐。 / Selection Back and gameplay middle-return each present one destination; closing the window saves without returning.
        run(enter + "tap:80:70,wait:400,window-close", 1)
        run(enter + f"tap:340:{start_y},wait:1500,enter,wait:400,window-close", 1)
        run(enter + "wait:1500,window-close", 0)

print("game exit SDL: three selection-back/gameplay-middle routes consume one clear intent; idle/window-close adds none")
