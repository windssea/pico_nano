"""原生资源完整校验后安装，坏资源不进入用户目录。 / Install after native validation; invalid resources never enter user directories."""
import pathlib,tempfile,subprocess,sys,zipfile,hashlib,struct
exe=sys.argv[1];repo=pathlib.Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as tmp:
 root=pathlib.Path(tmp)
 def check(source,kind,name,success):
  destination=root/('case-'+str(len(list(root.glob('case-*')))));destination.mkdir()
  r=subprocess.run([exe,str(destination),str(source),str(kind),name],capture_output=True,text=True,timeout=60)
  assert (r.returncode==0)==success,(name,r.stdout,r.stderr)
  folder={1:'books',2:'fonts',3:'covers',4:'wallpapers'}[kind];target=destination/folder/name
  assert target.exists()==success,(name,r.stdout)
  if success:assert hashlib.sha256(target.read_bytes()).digest()==hashlib.sha256(source.read_bytes()).digest()
  assert 'used=0 live=0' in r.stdout,(r.stdout,r.stderr)
 for encoding in ('utf-8','utf-16','gbk'):
  source=root/(encoding+'.txt');source.write_bytes(('上传保持原文。abc\n'*200).encode(encoding));check(source,1,'正文.txt',True)
 for source,name in ((repo/'assets/fonts/read-pico-ui.ttf','正文.ttf'),(repo/'tests/fixtures/font-fallback.ttf','备用.TTF')):check(source,2,name,True)
 font=bytearray((repo/'tests/fixtures/font-fallback.ttf').read_bytes());count=struct.unpack_from('>H',font,4)[0]
 for i in range(count):
  at=12+16*i
  if font[at:at+4]==b'glyf':offset=struct.unpack_from('>I',font,at+8)[0];struct.pack_into('>H',font,offset,0x7fff);break
 bad=root/'bad-font.ttf';bad.write_bytes(font);check(bad,2,'坏字体.ttf',False)
 for file,kind in (('image-black.png',3),('jpeg-baseline.jpg',4),('jpeg-progressive.jpg',4)):check(repo/'tests/fixtures'/file,kind,file,True)
 bad=root/'bad.png';data=(repo/'tests/fixtures/image-black.png').read_bytes();bad.write_bytes(data[:-4]);check(bad,4,'坏图片.png',False)
 check(repo/'tests/fixtures/jpeg-baseline.jpg',4,'伪扩展名.png',False)
 epub=root/'book.epub'
 with zipfile.ZipFile(epub,'w',compression=zipfile.ZIP_DEFLATED) as z:
  z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
  z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
  z.writestr('OPS/book.opf','<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="2.0" unique-identifier="uid"><metadata><d:title>传书</d:title><d:identifier id="uid">upload</d:identifier></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="a"/></spine></package>')
  z.writestr('OPS/a.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><body><p>传书验证</p></body></html>')
 check(epub,1,'小说.epub',True)
 damaged=bytearray(epub.read_bytes())
 with zipfile.ZipFile(epub) as z:info=z.getinfo('OPS/a.xhtml');at=info.header_offset;name_len,extra_len=struct.unpack_from('<HH',damaged,at+26);offset=at+30+name_len+extra_len;damaged[offset+info.compress_size//2]^=1
 bad=root/'bad.epub';bad.write_bytes(damaged);check(bad,1,'坏小说.epub',False)
 bad=root/'binary.txt';bad.write_bytes(b'\x00binary');check(bad,1,'二进制.txt',False)
print('Native resource upload: UTF8/UTF16/GBK, all TTF outlines, EPUB resource CRC, complete PNG/JPEG, bad glyph/image/content rejection and exact installed SHA passed')
