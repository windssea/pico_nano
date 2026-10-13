"""原创语义缓存样本：样式、空白、锚点、图片与损坏尾部。 / Original styled semantics and malformed-tail fixtures."""
import pathlib, subprocess, sys, tempfile, zipfile
with tempfile.TemporaryDirectory() as folder:
    book = pathlib.Path(folder) / "semantics.epub"
    head = '<html xmlns="http://www.w3.org/1999/xhtml"><head><link rel="stylesheet" href="a.css"/><style>p.x {font-style:italic}</style></head><body>'
    text = '<p class="x" id="start">中文 A   B &#x1f600; ' + '正文 &amp; 内容 ' * 700 + '<b>粗体</b> 尾部</p><pre> 空格\n换行 </pre><img src="image.png" alt="替代文字"/><div style="display:none" id="hidden">隐藏</div><p style="text-align:center">末段</p>'
    with zipfile.ZipFile(book, "w", compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        z.writestr("META-INF/container.xml", '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        names = [("a.xhtml", "application/xhtml+xml"), ("b.xhtml", "application/xhtml+xml"), ("bad.xhtml", "application/xhtml+xml"), ("a.css", "text/css"), ("image.png", "image/png")]
        z.writestr("book.opf", '<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="2.0" unique-identifier="u"><metadata><d:title>Original semantic test</d:title><d:identifier id="u">original-semantics</d:identifier></metadata><manifest>' + ''.join(f'<item id="i{i}" href="{name}" media-type="{kind}"/>' for i, (name, kind) in enumerate(names)) + '</manifest><spine><itemref idref="i0"/><itemref idref="i1"/><itemref idref="i2"/></spine></package>')
        z.writestr("a.xhtml", head + text + '</body></html>')
        z.writestr("b.xhtml", head + '<p>另一章节</p></body></html>')
        z.writestr("bad.xhtml", head + text + '</wrong></html>')
        z.writestr("a.css", 'p {font-weight:bold} p.x {text-align:right} img {width:60%;max-height:80px}')
        z.writestr("image.png", b"source identity only; no image decoding in this test")
    subprocess.run([sys.argv[1], str(book)], check=True, timeout=30)
