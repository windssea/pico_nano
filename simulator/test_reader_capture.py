"""实际TXT渲染与页锚点测试。 / Real TXT rendering and page-anchor tests."""
import pathlib
import re
import subprocess
import sys
import tempfile
with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    book = root / "book.txt"
    book.write_text("Read Pico reader. The next page preserves the source location.\n" * 200, encoding="utf8")
    outputs = []
    for page in [1, 2]:
        capture = root / f"page{page}.pgm"
        run = subprocess.run([sys.argv[1], "--headless", "--book", str(book), "--page", str(page), "--capture", str(capture)], capture_output=True, text=True)
        assert run.returncode == 0, run.stderr
        assert "used=0 live=0" in run.stdout
        match = re.search(r"reader: page=\d+ begin=(\d+) end=(\d+)", run.stdout)
        assert match, run.stdout
        outputs.append((tuple(map(int, match.groups())), capture.read_bytes()))
    assert outputs[0][0][1] == outputs[1][0][0]
    assert outputs[0][1] != outputs[1][1]
    assert len(set(outputs[0][1].split(b"\n", 3)[3])) > 2
    failed = root / "failed.pgm"
    run = subprocess.run([sys.argv[1], "--headless", "--book", str(book), "--budget", "500000", "--capture", str(failed)], capture_output=True, text=True)
    assert run.returncode != 0 and "used=0 live=0" in run.stdout and not failed.exists()
print("reader capture: actual glyphs, adjacent source anchors and budget cleanup passed")
