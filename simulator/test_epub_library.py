"""混合书架、EPUB续读/历史和内容改变。 / Mixed shelf, EPUB restore/history and changed content."""
import pathlib,tempfile,zipfile,subprocess,sys,os,re,hashlib
with tempfile.TemporaryDirectory() as folder:
 root=pathlib.Path(folder);books=root/'books';books.mkdir();book=books/'a.epub';state=root/'state'
 (books/'b.txt').write_text('阅读测试。\\n'*200,encoding='utf-8')
 with zipfile.ZipFile(book,'w',compression=zipfile.ZIP_DEFLATED) as z:
  z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
  z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
  z.writestr('OPS/book.opf','<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="2.0" unique-identifier="uid"><metadata><d:title>测试书</d:title><d:identifier id="uid">test</d:identifier></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="a"/></spine></package>')
  z.writestr('OPS/a.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><body>'+''.join(f'<p>阅读位置 {i:04d}，混合书架测试保留原文位置。</p>' for i in range(200))+'</body></html>')
 def run(script):
  r=subprocess.run([sys.argv[1],'--library',str(books),'--state-dir',str(state)],env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT=script),capture_output=True,text=True,timeout=40)
  assert r.returncode==0,(r.stdout,r.stderr)
  assert 'used=0 live=0' in r.stdout,r.stdout
  return r
 r=run('enter,next,back,continue,back,quit')
 positions=re.findall(r'epub_library_position path=(\S+) element=(\d+) run=(\d+) offset=(\d+)',r.stdout)
 assert len(positions)==2 and positions[0]!=positions[1],r.stdout
 assert r.stdout.count('library_open name=a.epub')==2 and 'recent_identity status=0' in r.stdout,r.stdout
 r=run('continue,back,quit');assert re.findall(r'epub_library_position path=(\S+) element=(\d+) run=(\d+) offset=(\d+)',r.stdout)==[positions[1]],r.stdout
 r=run('enter,back,tap:200:350,next,back,tap:200:200,back,quit')
 assert r.stdout.count('library_open name=a.epub')==2 and 'library_open name=b.txt' in r.stdout,r.stdout
 before={p.name:p.read_bytes() for p in state.iterdir()}
 with zipfile.ZipFile(book) as z:entries={n:z.read(n) for n in z.namelist()}
 entries['OPS/a.xhtml']=entries['OPS/a.xhtml'].replace('阅读位置'.encode(),'位置改变'.encode(),1)
 with zipfile.ZipFile(book,'w') as z:
  for n,data in entries.items():z.writestr(n,data,compress_type=zipfile.ZIP_STORED)
 r=run('continue,quit')
 assert 'recent_identity status=7' in r.stdout and 'library_open name=a.epub' not in r.stdout,r.stdout
 assert before=={p.name:p.read_bytes() for p in state.iterdir()}
 print('EPUB library: mixed shelf, real reading/continue/process restore, recent identity and changed source rejection passed')
