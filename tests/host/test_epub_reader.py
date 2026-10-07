"""真实EPUB资源中的阅读会话。 / Reader sessions in real EPUB resources."""
import pathlib,subprocess,tempfile,zipfile,sys
with tempfile.TemporaryDirectory() as folder:
 p=pathlib.Path(folder)/'book.epub'
 with zipfile.ZipFile(p,'w',compression=zipfile.ZIP_DEFLATED) as z:
  z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
  z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
  names=['a','empty','note','b','c','long'];items=''.join(f'<item id="{n}" href="{n}.xhtml" media-type="application/xhtml+xml"/>' for n in names)
  spine=''.join(f'<itemref idref="{n}"'+(' linear="no"' if n in ['note','long'] else '')+'/>' for n in names)
  z.writestr('OPS/book.opf','<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="2.0" unique-identifier="uid"><metadata><d:title>test</d:title><d:identifier id="uid">test</d:identifier></metadata><manifest>'+items+'</manifest><spine>'+spine+'</spine></package>')
  bodies={'a':'<p>A<span>B</span>CDEFGH</p>','empty':'<p/>','note':'<p>ZZ</p>','b':'<p id="target">IJKL</p>','c':'<p>MNO</p>','long':'<p>'+''.join(chr(65+i%26) for i in range(80))+'</p>'}
  for n,body in bodies.items():z.writestr(f'OPS/{n}.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><body>'+body+'</body></html>')
 r=subprocess.run([sys.argv[1],str(p)],capture_output=True,timeout=30)
 assert r.returncode==0,(r.stdout,r.stderr)
 print(r.stdout.decode())

 state=pathlib.Path(folder)/'state'
 import hashlib
 def resume(path,mode,success=True):
  r=subprocess.run([sys.argv[1],str(path),str(state),mode],capture_output=True,timeout=30)
  assert (r.returncode==0)==success,(r.stdout,r.stderr)
  assert b'used=0 live=0' in r.stderr
  if success:assert b'resume element=3 run=1 offset=0 dirty=0' in r.stdout
  return r
 resume(p,'write');before={path.name:path.read_bytes() for path in state.iterdir()}
 assert before and any(hashlib.sha256(p.read_bytes()).digest() in raw for raw in before.values())
 resume(p,'read');assert before=={path.name:path.read_bytes() for path in state.iterdir()}
 alias=pathlib.Path(folder)/'别名.epub';alias.write_bytes(p.read_bytes());resume(alias,'read')
 with zipfile.ZipFile(alias) as z:changed={name:z.read(name) for name in z.namelist()}
 changed['OPS/a.xhtml']=changed['OPS/a.xhtml'].replace(b'>A<',b'>Z<')
 with zipfile.ZipFile(alias,'w') as z:
  for name,data in changed.items():z.writestr(name,data,compress_type=zipfile.ZIP_STORED)
 r=resume(alias,'read',False);assert b'resume status=7' in r.stderr and not r.stdout
 assert before=={path.name:path.read_bytes() for path in state.iterdir()}
 with zipfile.ZipFile(p) as z:entries={name:z.read(name) for name in z.namelist()}
 original=entries['OPS/a.xhtml']
 entries['OPS/a.xhtml']=original.replace(b'</p></body>',b'</body>')
 with zipfile.ZipFile(p,'w') as z:
  for name,data in entries.items():z.writestr(name,data,compress_type=zipfile.ZIP_STORED)
 r=subprocess.run([sys.argv[1],str(p),'bad-xml'],capture_output=True,timeout=30);assert r.returncode==0,(r.stdout,r.stderr)
 entries['OPS/a.xhtml']=original
 with zipfile.ZipFile(p,'w') as z:
  for name,data in entries.items():z.writestr(name,data,compress_type=zipfile.ZIP_STORED)
 with zipfile.ZipFile(p) as z:offset=z.getinfo('OPS/a.xhtml').header_offset
 import struct
 raw=bytearray(p.read_bytes());n,e=struct.unpack_from('<HH',raw,offset+26);start=offset+30+n+e;at=raw.index(b'>A<',start);raw[at+1]=ord('Z');p.write_bytes(raw)
 r=subprocess.run([sys.argv[1],str(p),'bad-crc'],capture_output=True,timeout=30);assert r.returncode==0,(r.stdout,r.stderr)
