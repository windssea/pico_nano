"""原创混排与不连续文本位置，原分页对照索引定位。 / Original mixed flow and nonconsecutive text positions compare legacy paging with compiler seeking."""
import pathlib
import subprocess
import sys
import tempfile
import zipfile

with tempfile.TemporaryDirectory(prefix='pn-index-replay-') as directory:
    book = pathlib.Path(directory) / 'book.epub'
    with zipfile.ZipFile(book, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('mimetype', 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
        archive.writestr('META-INF/container.xml', '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        paths = ['c0.xhtml', 'c1.xhtml', 'c2.xhtml', 'bad.xhtml']
        archive.writestr('book.opf', '<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="3.0" unique-identifier="u"><metadata><d:title>Compiler replay</d:title><d:identifier id="u">original-CC0</d:identifier></metadata><manifest>' + ''.join(f'<item id="i{n}" href="{path}" media-type="application/xhtml+xml"/>' for n, path in enumerate(paths)) + '</manifest><spine>' + ''.join(f'<itemref idref="i{n}"/>' for n in range(len(paths))) + '</spine></package>')
        body = ''.join('<div><p>Start &amp;  A&#x4e00;B <b>bold text</b><span style="display:none">hidden</span> more words to wrap.</p><p><img src="x.png"/>Last<br/>line.</p></div>' for _ in range(90))
        for n in range(3):
            archive.writestr(f'c{n}.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml"><body><h2>Heading</h2>' + (body if n != 1 else '<p>' + 'long single paragraph with entities &amp;  words. ' * 120 + '</p>') + '</body></html>')
        archive.writestr('bad.xhtml', '<wrong xmlns="http://www.w3.org/1999/xhtml"><body><p>' + 'never trust unvalidated tails ' * 1000 + '</p></body></wrong>')
    subprocess.run([sys.argv[1], str(book)], check=True, timeout=90)
