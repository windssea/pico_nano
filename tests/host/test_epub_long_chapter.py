"""原创超长单章（约30万中文标量）验证翻页语义与解析次数，不以桌面时间当硬件指标。 / Original very long single chapter (~300k CJK scalars) checks paging semantics and parse counts, without hardware timing claims."""
import pathlib,random,subprocess,sys,tempfile,zipfile
def make_book(book,paragraphs=2500):
    rng=random.Random(20261014);alphabet='天地玄黄宇宙洪荒日月盈昃辰宿列张寒来暑往秋收冬藏闰余成岁律吕调阳云腾致雨露结为霜金生丽水玉出昆冈'
    head='<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Original</title></head><body>'
    body=''.join('<p>'+''.join(rng.choices(alphabet,k=rng.randint(40,160)))+'</p>\n' for _ in range(paragraphs))
    with zipfile.ZipFile(book,'w',compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
        z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('book.opf','<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="3.0" unique-identifier="u"><metadata><d:title>Original long chapter</d:title><d:identifier id="u">pn-long-chapter</d:identifier></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/><item id="b" href="b.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="a"/><itemref idref="b"/></spine></package>')
        z.writestr('a.xhtml',head+body+'</body></html>');z.writestr('b.xhtml',head+'<p>终章</p></body></html>')
if __name__=='__main__':
    with tempfile.TemporaryDirectory() as folder:
        book=pathlib.Path(folder)/'long-chapter.epub';make_book(book);subprocess.run([sys.argv[1],str(book)],check=True,timeout=600)
