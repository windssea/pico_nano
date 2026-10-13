"""低预算阅读、回收及续读，独立状态不碰原书。 / Low-budget reading, eviction and resume with isolated state and immutable books."""
import hashlib
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import zipfile


def book(path, fmt):
    text = "阅读缓存压力，当前页和保存优先。" * 2000
    if fmt == "txt":
        path.write_text(text, encoding="utf-8")
        return
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        archive.writestr("META-INF/container.xml", '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        archive.writestr("OPS/book.opf", '<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="2.0" unique-identifier="uid"><metadata><d:title>缓存压力</d:title><d:identifier id="uid">cache</d:identifier></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="a"/></spine></package>')
        archive.writestr("OPS/a.xhtml", '<html xmlns="http://www.w3.org/1999/xhtml"><body><p>' + text + '</p></body></html>')


with tempfile.TemporaryDirectory() as folder:
    root = pathlib.Path(folder)
    for fmt in ("txt", "epub"):
        case = root / fmt
        books = case / "books"
        books.mkdir(parents=True)
        source = books / ("a." + fmt)
        book(source, fmt)
        digest = hashlib.sha256(source.read_bytes()).digest()
        env = dict(os.environ, SDL_VIDEODRIVER="dummy", PN_SIM_TIMING="1",
                   PN_SIM_LIBRARY_SCRIPT="library,wait:1000,enter,wait:1000,next,previous,next,back,continue,back,quit")
        result = subprocess.run([sys.argv[1], "--library", str(books), "--state-dir", str(case / "state"),
                                 "--cover-cache", str(case / "covers"), "--budget", str(2 * 1024 * 1024)],
                                env=env, capture_output=True, text=True, timeout=60)
        assert result.returncode == 0, (result.stdout, result.stderr)
        assert "used=0 live=0" in result.stdout, result.stdout
        assert result.stdout.count("library_open name=") == 2, (result.stdout, result.stderr)
        assert re.findall(r"library_turn pointer=\d status=(\d+)", result.stdout) == ["0", "0", "0"], result.stdout
        assert result.stdout.count("library_return status=0") == 2, result.stdout
        assert hashlib.sha256(source.read_bytes()).digest() == digest
        if fmt == "txt":
            positions = re.findall(r"library_position offset=(\d+)", result.stdout)
            confirmed = re.findall(r"library_confirmed offset=(\d+)", result.stdout)
            assert len(positions) == 2 and int(confirmed[-1]) > 0 and positions[1] == confirmed[-1], result.stdout
            for kind in (0, 1, 2):
                assert re.search(rf"cache .*kind={kind} .*evictions=[1-9]", result.stderr), result.stderr
        else:
            positions = re.findall(r"epub_library_position (.+)", result.stdout)
            assert len(positions) == 2 and positions[0] != positions[1], result.stdout
        aggregate = 0
        for line in result.stderr.splitlines():
            if line.startswith("cache "):
                fields = dict(re.findall(r"(\w+)=([^\s]+)", line))
                assert int(fields["bytes"]) <= [512, 768, 256][int(fields["kind"])] * 1024, line
                if fields["kind"] == "0":
                    aggregate = 0
                aggregate += int(fields["bytes"])
                if fields["kind"] == "2":
                    assert aggregate <= 1024 * 1024, line
print("2MiB TXT/EPUB: foreground turns, eviction, source preservation, real close/save/resume and zero live allocations passed")
