"""与独立hashlib比较流式摘要。 / Compare streamed digests with independent hashlib."""
import hashlib
import pathlib
import random
import subprocess
import sys
import tempfile
rng = random.Random(20261005)
with tempfile.TemporaryDirectory() as directory:
    path = pathlib.Path(directory) / "book"
    for size in [0, 1, 55, 56, 63, 64, 65, 119, 120, 127, 128, 4095, 4096, 4097, 65536, 1000000]:
        data = b"a" * size if size == 1000000 else rng.randbytes(size)
        path.write_bytes(data)
        result = subprocess.run([sys.argv[1], str(path)], text=True, capture_output=True, check=True)
        assert result.stdout.strip() == hashlib.sha256(data).hexdigest(), (size, result.stdout)
print("SHA-256: 16 independent vectors including million-a and block/read boundaries")
