"""与Python zipfile比对流式stored/deflate和故障。 / Streaming stored/deflate oracle and malformed-container checks."""
import io
import pathlib
import random
import subprocess
import sys
import tempfile
import warnings
import zipfile
import struct
import os
import re
binary = sys.argv[1]
with tempfile.TemporaryDirectory() as directory:
    path = pathlib.Path(directory) / "book.epub"
    payload = ("这是流式章节。Read Pico.\n" * 30000).encode() + random.Random(42).randbytes(50000)
    for method in [zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED]:
        with zipfile.ZipFile(path, "w", compression=method) as z:
            z.writestr("mimetype", b"application/epub+zip", compress_type=zipfile.ZIP_STORED)
            z.writestr("文本/正文.xhtml", payload)
            z.writestr("empty", b"")
        for chunk in [1, 17, 4096]:
            result = subprocess.run([binary, str(path), "文本/正文.xhtml", str(chunk)], capture_output=True, timeout=40)
            assert result.returncode == 0 and result.stdout == payload and b"used=0 live=0" in result.stderr, result.stderr
        empty = subprocess.run([binary, str(path), "empty"], capture_output=True, timeout=10)
        assert empty.returncode == 0 and empty.stdout == b"", empty.stderr
    def rejected(data):
        path.write_bytes(data)
        result = subprocess.run([binary, str(path), "mimetype"], capture_output=True, timeout=10)
        assert result.returncode != 0 and b"used=0 live=0" in result.stderr, result.stderr
    for name in ["../bad", "/bad", "a/../bad", "a\\bad"]:
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, "w") as z:
            z.writestr("mimetype", b"application/epub+zip")
            z.writestr(name, b"bad")
        rejected(buf.getvalue())
    buf = io.BytesIO()
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        with zipfile.ZipFile(buf, "w") as z:
            z.writestr("mimetype", b"application/epub+zip")
            z.writestr("mimetype", b"duplicate")
    rejected(buf.getvalue())
    # 不可seek输出强制data descriptor。/ Unseekable output forces data descriptors.
    class Unseekable(io.BytesIO):
        def seekable(self):
            return False
        def seek(self, *args):
            raise io.UnsupportedOperation
    buf = Unseekable()
    with zipfile.ZipFile(buf, "w", compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr("mimetype", b"application/epub+zip")
        z.writestr("chapter", payload)
    path.write_bytes(buf.getvalue())
    streamed = subprocess.run([binary, str(path), "chapter", "17"], capture_output=True, timeout=30)
    assert streamed.returncode == 0 and streamed.stdout == payload, streamed.stderr
    detached = subprocess.run([binary, str(path), "chapter", "17", "detach"], capture_output=True, timeout=10)
    assert detached.returncode != 0 and b"status=6" in detached.stderr and b"used=0 live=0" in detached.stderr, detached.stderr
    attempts = int(re.search(rb"attempts=(\d+)", streamed.stderr)[1])
    for fault in range(1, attempts + 1):
        result = subprocess.run([binary, str(path), "chapter", "4096"], env=dict(os.environ, PN_ZIP_FAIL_AT=str(fault)), capture_output=True, timeout=10)
        assert result.returncode != 0 and b"used=0 live=0" in result.stderr, result.stderr
    # 部分头和deflate破坏不可变成成功空输出。/ Header/deflate damage must never become successful empty output.
    original = buf.getvalue()
    for offset in [0, original.index(b"PK\x01\x02"), len(original)-22]:
        bad = bytearray(original);bad[offset] ^= 1;rejected(bad)
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", compression=zipfile.ZIP_STORED) as z:
        z.writestr("mimetype", b"application/epub+zip")
    original = buf.getvalue()
    bad = bytearray(original);bad[30+len("mimetype")] ^= 1;rejected(bad)
    for central_offset, value in [(8, 1), (10, 99), (20, 0xffffffff), (42, 0xffffffff)]:
        bad = bytearray(original);at = original.index(b"PK\x01\x02")+central_offset
        struct.pack_into("<H" if central_offset<16 else "<I", bad, at, value);rejected(bad)
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr("mimetype", b"application/epub+zip")
    bad = bytearray(buf.getvalue());bad[30+len("mimetype")] ^= 0xff;rejected(bad)
    # 大目录不复制所有名称。/ Large directories do not copy every name.
    with zipfile.ZipFile(path, "w") as z:
        for i in range(32768):
            z.writestr(f"part{i:05}.xhtml", b"x")
    large = subprocess.run([binary, str(path), "part32767.xhtml"], capture_output=True, timeout=30)
    assert large.returncode == 0 and large.stdout == b"x" and b"used=0 live=0" in large.stderr, large.stderr
print("zip: stored/deflate, Unicode paths, empty entries, small chunks, traversal and duplicates passed")
