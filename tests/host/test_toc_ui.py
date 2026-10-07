"""真实EPUB3层级目录界面。 / Actual EPUB3 hierarchical TOC UI."""
import pathlib,zipfile,tempfile,subprocess,sys
with tempfile.TemporaryDirectory() as folder:
 p=pathlib.Path(folder)/'book.epub'
 with zipfile.ZipFile(p,'w',compression=zipfile.ZIP_DEFLATED) as z:
  z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
  z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
  z.writestr('OPS/book.opf','<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="3.0" unique-identifier="uid"><metadata><d:title>目录测试</d:title><d:identifier id="uid">test</d:identifier></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/><item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/></manifest><spine><itemref idref="a"/></spine></package>')
  nav='<nav epub:type="toc"><ol><li><span>正文分组</span><ol>'+''.join(f'<li><a href="a.xhtml#p{i}">第{i}节 阅读目录测试</a></li>' for i in range(7))+'</ol></li></ol></nav>'
  z.writestr('OPS/nav.xhtml','<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><body>'+nav+'</body></html>')
  body=''.join(f'<h2 id="p{i}">第{i}节</h2>'+('<p>阅读文字，这是目录跳转测试。</p>'*30) for i in range(7))
  z.writestr('OPS/a.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><body>'+body+'</body></html>')
 r=subprocess.run([sys.argv[1],str(p),sys.argv[2]],capture_output=True,timeout=30)
 assert r.returncode==0,(r.stdout,r.stderr)
 print(r.stdout.decode())
