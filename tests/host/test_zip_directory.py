"""原创500条目ZIP目录往返样本。 / Original 500-entry ZIP seek fixture."""
import pathlib, tempfile, zipfile, subprocess, sys
with tempfile.TemporaryDirectory() as folder:
    p = pathlib.Path(folder) / "directory.zip"
    with zipfile.ZipFile(p, "w", compression=zipfile.ZIP_STORED) as z:
        for i in range(500):
            z.writestr(f"resource-{i:04}.bin", bytes([i & 255]) * 80)
    subprocess.run([sys.argv[1], str(p)], check=True, timeout=30)
