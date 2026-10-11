"""大封面首帧在默认预算内可打开，失败可见且能返回书架。 / Large-cover first frame opens within the default budget; failures are visible and recoverable."""
import os
import pathlib
import subprocess
import sys
import tempfile
import zipfile

with tempfile.TemporaryDirectory() as folder:
    root = pathlib.Path(folder)
    books = root / "books"
    books.mkdir()
    fixture = pathlib.Path(__file__).parents[1] / "tests/fixtures/jpeg-cover-large.jpg"
    with zipfile.ZipFile(books / "large.epub", "w", compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        z.writestr("META-INF/container.xml", '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        chapters = 480
        items = ''.join(f'<item id="c{i}" href="c{i}.xhtml" media-type="application/xhtml+xml"/>' for i in range(chapters))
        spine = ''.join(f'<itemref idref="c{i}"/>' for i in range(chapters))
        z.writestr("OPS/book.opf", f'<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="2.0" unique-identifier="uid"><metadata><d:identifier id="uid">large-cover</d:identifier><d:title>大封面测试</d:title></metadata><manifest>{items}<item id="cover" href="cover.jpg" media-type="image/jpeg"/></manifest><spine>{spine}</spine></package>')
        z.writestr("OPS/cover.jpg", fixture.read_bytes())
        for i in range(chapters):
            body = '<img src="cover.jpg"/>' if not i else '<p>阅读测试。继续翻页，返回书架。</p>' * 50
            z.writestr(f"OPS/c{i}.xhtml", f'<html xmlns="http://www.w3.org/1999/xhtml"><head><title>测试</title></head><body>{body}</body></html>')
    # 大图缩略图也须在书架空闲期间出现，不能只测正文可打开。/ Large-image thumbnails must appear while the shelf is idle, not merely open in reading.
    cache = root / "covers"
    cover_args = [sys.argv[1], "--library", str(books), "--state-dir", str(root / "cover-state"), "--cover-cache", str(cache)]
    covered = subprocess.run(cover_args,
        env=dict(os.environ, SDL_VIDEODRIVER="dummy", PN_SIM_LIBRARY_SCRIPT="wait:1500,quit"),
        capture_output=True, text=True, timeout=60)
    assert covered.returncode == 0 and "covers ready=1 status=0" in covered.stdout, (covered.stdout, covered.stderr)
    assert "used=0 live=0" in covered.stdout, covered.stdout
    assert len(list(cache.glob("*.pnc"))) == 1
    warm = subprocess.run(cover_args, env=dict(os.environ, SDL_VIDEODRIVER="dummy", PN_SIM_LIBRARY_SCRIPT="wait:300,quit"),
        capture_output=True, text=True, timeout=60)
    assert warm.returncode == 0 and "covers ready=" not in warm.stdout and "used=0 live=0" in warm.stdout, (warm.stdout, warm.stderr)
    result = subprocess.run([sys.argv[1], "--library", str(books), "--state-dir", str(root / "state")],
        env=dict(os.environ, SDL_VIDEODRIVER="dummy", PN_SIM_LIBRARY_SCRIPT="tap:100:500,next,back,quit"),
        capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert "library_open name=large.epub" in result.stdout, (result.stdout, result.stderr)
    assert "library_turn pointer=0 status=0" in result.stdout, result.stdout
    assert "library_return status=0" in result.stdout, result.stdout
    assert "used=0 live=0" in result.stdout, result.stdout
    # 损坏书籍必须提示失败，不能像点击没有反应；不能泄漏资源。/ Corrupt books must report failure instead of appearing unresponsive, without leaking resources.
    bad = root / "bad"
    bad.mkdir()
    (bad / "broken.epub").write_bytes(b"not a ZIP")
    result = subprocess.run([sys.argv[1], "--library", str(bad), "--state-dir", str(root / "bad-state")],
        env=dict(os.environ, SDL_VIDEODRIVER="dummy", PN_SIM_LIBRARY_SCRIPT="tap:100:500,quit"),
        capture_output=True, text=True, timeout=60)
    assert result.returncode == 0 and "shelf_open_failed status=" in result.stdout and "notice=1" in result.stdout, (result.stdout, result.stderr)
    assert "used=0 live=0" in result.stdout, result.stdout
print("large EPUB window: first page, next page and return under the default budget passed")
