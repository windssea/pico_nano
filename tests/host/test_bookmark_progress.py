"""多章节EPUB书签进度回归。 / Multi-section EPUB bookmark progress regression."""
import pathlib,subprocess,sys,tempfile,zipfile
with tempfile.TemporaryDirectory() as folder:
 root=pathlib.Path(folder);book=root/'book.epub'
 with zipfile.ZipFile(book,'w',compression=zipfile.ZIP_DEFLATED) as z:
  z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
  z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
  z.writestr('OPS/book.opf','<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="2.0" unique-identifier="uid"><metadata><d:title>进度测试</d:title><d:identifier id="uid">progress</d:identifier></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/><item id="b" href="b.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="a"/><itemref idref="b"/></spine></package>')
  for name in ('a','b'):z.writestr(f'OPS/{name}.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><body>'+('<p>测试阅读进度保持，添加书签不移动原文。</p>'*100)+'</body></html>')
 result=subprocess.run([sys.argv[1],str(book),str(root/'state'),sys.argv[2]],capture_output=True,text=True,timeout=30)
 assert result.returncode==0,(result.stdout,result.stderr)
 print(result.stdout)
