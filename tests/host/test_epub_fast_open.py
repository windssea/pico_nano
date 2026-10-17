"""EPUB快开：带首页插图的书，快开首屏与完整打开一致。 / EPUB fast open: a book with an opening illustration renders the same first screen as a full open."""
import pathlib, subprocess, sys, tempfile, zipfile

with tempfile.TemporaryDirectory() as folder:
    root = pathlib.Path(folder); book = root / 'book.epub'; state = root / 'state'; state.mkdir()
    png = (pathlib.Path(__file__).resolve().parents[1] / 'fixtures' / 'image-black.png').read_bytes()
    with zipfile.ZipFile(book, 'w', compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr('mimetype', 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
        z.writestr('META-INF/container.xml', '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('OPS/book.opf', '<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="2.0" unique-identifier="uid"><metadata><d:title>快开</d:title><d:identifier id="uid">fast</d:identifier></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/><item id="img" href="a.png" media-type="image/png"/></manifest><spine><itemref idref="a"/></spine></package>')
        z.writestr('OPS/a.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml"><body><img src="a.png"/>' + ''.join(f'<p>第{i}段，快开首屏与完整打开必须一致。</p>' for i in range(400)) + '</body></html>')
        z.writestr('OPS/a.png', png)
    r = subprocess.run([sys.argv[1], str(book), str(state)], capture_output=True, timeout=120)
    assert r.returncode == 0, (r.stdout, r.stderr)
    print(r.stdout.decode())
