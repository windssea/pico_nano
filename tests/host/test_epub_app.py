"""真实字体/图片装配验证。 / Actual font/image application wiring."""
import pathlib,tempfile,subprocess,zipfile,sys
with tempfile.TemporaryDirectory() as folder:
 root=pathlib.Path(folder);book=root/'book.epub';state=root/'state'
 png=(pathlib.Path(sys.argv[2]).parents[2]/'tests'/'fixtures'/'image-black.png').read_bytes()
 with zipfile.ZipFile(book,'w',compression=zipfile.ZIP_DEFLATED) as z:
  z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
  z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
  z.writestr('OPS/book.opf','<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="2.0" unique-identifier="uid"><metadata><d:title>测试书</d:title><d:identifier id="uid">test</d:identifier></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/><item id="img" href="a.png" media-type="image/png"/><item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest><spine toc="ncx"><itemref idref="a"/></spine></package>')
  body='<h2>阅读测试</h2><img src="a.png"/>'+''.join(f'<p id="p{i}">阅读位置 {i:04d}，这是原生阅读页面的字体与段落测试。</p>' for i in range(200))
  z.writestr('OPS/a.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><body>'+body+'</body></html>')
  z.writestr('OPS/a.png',png)
  z.writestr('OPS/toc.ncx','<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1"><navMap><navPoint id="one"><navLabel><text>中部</text></navLabel><content src="a.xhtml#p100"/></navPoint><navPoint id="two"><navLabel><text>缺失锚点</text></navLabel><content src="a.xhtml#absent"/></navPoint></navMap></ncx>')

 r=subprocess.run([sys.argv[1],str(book),str(state),sys.argv[2]],capture_output=True,timeout=60)
 assert r.returncode==0,(r.stdout,r.stderr)
 print(r.stdout.decode())
