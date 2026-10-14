"""原创长章由1000个短段构成，不以桌面速度当硬件指标。 / Original long chapter of 1000 short paragraphs, without hardware speed claims."""
import pathlib,subprocess,sys,tempfile,zipfile
def make_book(book):
    head='<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Original</title></head><body>'
    text=''.join('<p>短段落也需要节约内存保持阅读流畅。</p>' for _ in range(1000))
    with zipfile.ZipFile(book,'w',compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
        z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('book.opf','<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="3.0" unique-identifier="u"><metadata><d:title>Original short runs</d:title><d:identifier id="u">pn-short-runs</d:identifier></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/><item id="b" href="bad.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="a"/><itemref idref="b"/></spine></package>')
        z.writestr('a.xhtml',head+text+'</body></html>');z.writestr('bad.xhtml',head+text+'</wrong></html>')
if __name__=='__main__':
    with tempfile.TemporaryDirectory() as folder:
        book=pathlib.Path(folder)/'short-runs.epub';make_book(book);subprocess.run([sys.argv[1],str(book)],check=True,timeout=60)
