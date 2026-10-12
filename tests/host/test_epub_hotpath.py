"""原创大目录EPUB的翻页I/O与热恢复回归。 / Page I/O and warm-restore regression on an original large-spine EPUB."""
import pathlib
import subprocess
import sys
import tempfile
import zipfile
with tempfile.TemporaryDirectory() as directory:
    root=pathlib.Path(directory);book=root/'large.epub';state=root/'state';state.mkdir()
    names=[f'chapter-{i:04d}.xhtml' for i in range(400)]
    with zipfile.ZipFile(book,'w',compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
        z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        items=''.join(f'<item id="c{i}" href="{name}" media-type="application/xhtml+xml"/>' for i,name in enumerate(names))
        spine=''.join(f'<itemref idref="c{i}"/>' for i in range(400))
        z.writestr('book.opf','<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="uid"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Original performance fixture</dc:title><dc:identifier id="uid">pn-hotpath-1</dc:identifier><dc:language>en</dc:language></metadata><manifest>'+items+'</manifest><spine>'+spine+'</spine></package>')
        for i,name in enumerate(names):
            paragraphs=''.join(f'<p>Original text {i} {j}: a bounded reader must avoid rereading publication metadata on every turn.</p>' for j in range(80))
            z.writestr(name,'<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Original</title></head><body>'+paragraphs+'</body></html>')
    subprocess.run([sys.argv[1],str(book),str(state)],check=True,timeout=90)
