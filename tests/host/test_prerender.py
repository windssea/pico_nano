"""原创跨章和混合字号预绘制样本，不使用私人原书。 / Original chapter/mixed-size fixtures, without private books."""
import html
import pathlib
import random
import subprocess
import sys
import tempfile
import zipfile

alphabet = '天地玄黄宇宙洪荒日月盈昃辰宿列张寒来暑往秋收冬藏闰余成岁律吕调阳云腾致雨露结为霜金生丽水玉出昆冈剑号巨阙珠称夜光果珍李柰菜重芥姜海咸河淡鳞潜羽翔龙师火帝鸟官人皇始制文字乃服衣裳推位让国有虞陶唐'
with tempfile.TemporaryDirectory() as folder:
    rng = random.Random(20261015)
    paragraphs = [''.join(rng.choices(alphabet, k=180)) for _ in range(120)]
    book = pathlib.Path(folder) / 'prerender.txt'
    book.write_text('\n'.join(paragraphs), encoding='utf-8')
    epub = pathlib.Path(folder) / 'prerender.epub'
    with zipfile.ZipFile(epub, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('mimetype', 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
        archive.writestr('META-INF/container.xml', '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        archive.writestr('book.opf', '<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="3.0" unique-identifier="u"><metadata><d:title>Original prerender</d:title><d:identifier id="u">pn-prerender</d:identifier></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/><item id="b" href="b.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="a"/><itemref idref="b"/></spine></package>')
        for name, source in [('a.xhtml', paragraphs[:3]), ('b.xhtml', paragraphs[3:])]:
            body = ''.join(('<h2>章间混合字号</h2>' if i % 5 == 0 else '') + '<p>' + html.escape(p) + '</p>' for i, p in enumerate(source))
            archive.writestr(name, '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Original</title></head><body>' + body + '</body></html>')
    subprocess.run([sys.argv[1], str(book), str(epub)], check=True, timeout=300)
