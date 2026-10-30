"""原创文本/图片相邻章，覆盖PNG与渐进JPEG，保持原始图片字节。 / Original adjacent text/image chapters with PNG and progressive JPEG, preserving source bytes."""
from pathlib import Path
import struct,subprocess,sys,tempfile,zipfile,zlib
def chunk(k,b):return struct.pack('>I',len(b))+k+b+struct.pack('>I',zlib.crc32(k+b))
raw=b''.join(b'\0'+bytes((x*7+y*13)%256 for x in range(173)) for y in range(391))
png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',173,391,8,0,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')
rgba=b''.join(b'\0'+bytes(v for x in range(173) for v in ((x*7+y*13)%256,(x*3+y*5)%256,(x*11+y)%256,(x*17+y*19)%256)) for y in range(391))
transparent=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',173,391,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(rgba))+chunk(b'IEND',b'')
with tempfile.TemporaryDirectory() as folder:
 for kind,pixels in [('png',png),('jpeg',Path(sys.argv[3]).read_bytes()),('png',transparent)]:
  path=Path(folder)/(kind+'.epub')
  with zipfile.ZipFile(path,'w',compression=zipfile.ZIP_DEFLATED) as z:
   z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
   z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
   z.writestr('book.opf',f'<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="3.0" unique-identifier="u"><metadata><d:title>Original image test</d:title><d:identifier id="u">{kind}</d:identifier></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/><item id="b" href="b.xhtml" media-type="application/xhtml+xml"/><item id="d" href="d.xhtml" media-type="application/xhtml+xml"/><item id="c" href="image.{kind}" media-type="image/{kind}"/></manifest><spine><itemref idref="a"/><itemref idref="b"/><itemref idref="d"/></spine></package>')
   z.writestr('a.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><body><p>Original text before an illustrated chapter.</p></body></html>')
   z.writestr('b.xhtml',f'<html xmlns="http://www.w3.org/1999/xhtml"><body><p>Original inline illustration.</p><img src="image.{kind}"/><p>Original text after the illustration.</p></body></html>')
   z.writestr('d.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><body><p>Original text following the illustrated chapter.</p></body></html>')
   z.writestr(f'image.{kind}',pixels)
  subprocess.run([sys.argv[1],str(path),sys.argv[2]],check=True,timeout=120)
