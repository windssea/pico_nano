"""检查共用C渲染器的捕获及超预算行为。 / Check shared C capture and over-budget behavior."""
import pathlib
import subprocess
import sys
import tempfile
with tempfile.TemporaryDirectory() as tmp:
    output = pathlib.Path(tmp) / "frame.pgm"
    run = subprocess.run([sys.argv[1], "--headless", "--capture", str(output)], capture_output=True, text=True)
    assert run.returncode == 0, run.stderr
    content = output.read_bytes()
    header = b"P5\n684 1216\n255\n"
    assert content.startswith(header)
    data = content[len(header):]
    assert len(data) == 684 * 1216
    assert set(data) == {i * 17 for i in range(16)}
    assert "live=0" in run.stdout
    output.unlink()
    scenario = subprocess.run([sys.argv[1], "--headless", "--scenario", "ownership", "--capture", str(output)], capture_output=True, text=True)
    assert scenario.returncode == 0, scenario.stderr
    assert "stale_completion=discarded" in scenario.stdout and "live=0" in scenario.stdout
    assert output.read_bytes() == content
    output.unlink()
    partial = subprocess.run([sys.argv[1], "--headless", "--scenario", "ownership", "--budget", "500000", "--capture", str(output)], capture_output=True, text=True)
    assert partial.returncode != 0 and not output.exists()
    assert "used=0 live=0" in partial.stdout
    denied = subprocess.run([sys.argv[1], "--headless", "--capture", str(output), "--budget", "4096"], capture_output=True)
    assert denied.returncode != 0 and not output.exists()
    for arg in ["-1", "no", "18446744073709551616"]:
        invalid = subprocess.run([sys.argv[1], "--headless", "--budget", arg], capture_output=True)
        assert invalid.returncode != 0
print("shared 4bpp capture, all grayscale levels, cleanup and budget failure passed")
