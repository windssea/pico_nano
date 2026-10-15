"""原创多章分组目录，主机/SDL共用。 / Original grouped multi-chapter TOC shared by host and SDL."""
import pathlib,zipfile
def create(path):
    path=pathlib.Path(path)
    with zipfile.ZipFile(path,'w',compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
        z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        manifest=''.join(f'<item id="c{i}" href="c{i:02}.xhtml" media-type="application/xhtml+xml"/>' for i in range(48))
        spine=''.join(f'<itemref idref="c{i}"/>' for i in range(48))
        z.writestr('OPS/book.opf',f'<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="3.0" unique-identifier="uid"><metadata><d:title>连续目录演练</d:title><d:identifier id="uid">original-toc-motion</d:identifier></metadata><manifest>{manifest}<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/></manifest><spine>{spine}</spine></package>')
        groups=''.join(f'<li><span>第{g}卷 目录分组</span><ol>'+''.join(f'<li><a href="c{i:02}.xhtml">第{i:02}章 阅读、位置与连续浏览的原创演练</a></li>' for i in range(g*8,g*8+8))+'</ol></li>' for g in range(6))
        z.writestr('OPS/nav.xhtml',f'<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><body><nav epub:type="toc"><ol>{groups}</ol></nav></body></html>')
        for i in range(48):z.writestr(f'OPS/c{i:02}.xhtml',f'<html xmlns="http://www.w3.org/1999/xhtml"><body><h1>第{i:02}章</h1>'+('<p>这是原创章节文字，用于验证目录浏览不改变阅读位置，确认跳转后才记录新章节。</p>'*12)+'</body></html>')
    return path
