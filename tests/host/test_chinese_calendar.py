"""逐日对照固定上游独立计算的年度摘要。/ Compare every day against annual digests independently calculated by pinned upstream."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory() as directory:
    output = Path(directory) / "calendar.tsv"
    result = subprocess.run([sys.argv[1], str(output)], capture_output=True, timeout=30)
    assert result.returncode == 0, (result.stdout, result.stderr)
    actual = {}
    count = 0
    for line in output.read_bytes().splitlines(keepends=True):
        year = line.split(b"\t", 1)[0].decode("ascii")
        actual.setdefault(year, hashlib.sha256()).update(line)
        count += 1
    expected = json.loads(Path(sys.argv[2]).read_text(encoding="utf-8"))["yearSha256"]
    assert {year: digest.hexdigest() for year, digest in actual.items()} == expected
    assert count == 47847
    print("Chinese calendar: 47,847 dates match independent upstream lunar, ganzhi, festival and full Yi/Ji digests")
