"""原创EPUB封面/正式元数据夹具，不依赖私人书籍。 / Original EPUB cover and publication metadata fixture, independent of private books."""
import pathlib
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib

with tempfile.TemporaryDirectory() as temporary:
    root = pathlib.Path(temporary)
    cache = root / 'cache'
    cache.mkdir()
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    rows = b''.join(b'\0' + bytes((x * 13 + y * 7) & 255 for x in range(184)) for y in range(256))
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 184, 256, 8, 0, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')
    book = root / 'book-file.epub'
    with zipfile.ZipFile(book, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('mimetype', 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
        archive.writestr('META-INF/container.xml', '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        archive.writestr('OPS/book.opf', '<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="uid"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Publication title</dc:title><dc:creator>Verified author</dc:creator><dc:identifier id="uid">opening-fixture</dc:identifier></metadata><manifest><item id="text" href="text.xhtml" media-type="application/xhtml+xml"/><item id="cover" href="cover.png" media-type="image/png" properties="cover-image"/></manifest><spine><itemref idref="text"/></spine></package>')
        archive.writestr('OPS/text.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Fixture</title></head><body><p>Original fixture.</p></body></html>')
        archive.writestr('OPS/cover.png', png)
    subprocess.run([sys.argv[1], str(cache), str(book)], check=True)
