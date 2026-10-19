"""真实产品TXT/EPUB统计路由，端口替身不代表实机。 / Real product statistics routes with ports substituted, not hardware evidence."""
import pathlib
import subprocess
import sys
import tempfile
import zipfile

with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    text = root / "book.txt"
    text.write_text("阅读统计测试。前后翻页与菜单、锁屏停留分别计量。\n" * 1000, encoding="utf-8")
    book = root / "book.epub"
    with zipfile.ZipFile(book, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        archive.writestr("META-INF/container.xml", '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        archive.writestr("book.opf", '<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="2.0" unique-identifier="uid"><metadata><d:title>阅读统计测试</d:title><d:identifier id="uid">stats</d:identifier></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="a"/></spine></package>')
        archive.writestr("a.xhtml", '<html xmlns="http://www.w3.org/1999/xhtml"><body>' + '<p>阅读统计测试。前后翻页与菜单、锁屏停留分别计量。</p>' * 1000 + '</body></html>')
    for source in (text, book):
        state = root / (source.suffix[1:] + "-state")
        result = subprocess.run([sys.argv[1], str(source), str(state), sys.argv[2]], capture_output=True, text=True, timeout=60)
        assert result.returncode == 0, (source, result.stdout, result.stderr)
        print(result.stdout)
