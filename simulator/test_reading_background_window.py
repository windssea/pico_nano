"""TXT/EPUB阅读背景的草稿、确认、重开与翻页。 / TXT/EPUB paper drafts, confirmed saves, reopen and turns."""
import os,pathlib,struct,subprocess,sys,tempfile,zipfile
with tempfile.TemporaryDirectory() as folder:
 root=pathlib.Path(folder)
 for kind in ('txt','epub'):
  books=root/kind;books.mkdir();state=root/(kind+'-state')
  text='Reading paper keeps the original glyph shapes, layout and anchors. '*200
  if kind=='txt':(books/'book.txt').write_text(text,encoding='utf-8')
  else:
   with zipfile.ZipFile(books/'book.epub','w',compression=zipfile.ZIP_DEFLATED) as z:
    z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
    z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
    z.writestr('book.opf','<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="3.0" unique-identifier="u"><metadata><d:title>Paper test</d:title><d:identifier id="u">paper-test</d:identifier></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="a"/></spine></package>')
    z.writestr('a.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><body><p>'+text+'</p></body></html>')
  def run(name,script):
   frames=root/(kind+'-'+name);frames.mkdir()
   result=subprocess.run([sys.argv[1],'--library',str(books),'--state-dir',str(state)],env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT='library,enter,'+script+'window-close',PN_SIM_FRAME_DIR=str(frames)),capture_output=True,text=True,timeout=120)
   assert result.returncode==0 and 'used=0 live=0' in result.stdout,(result.stdout,result.stderr)
   captures=sorted(frames.glob('frame-*.pgm'));assert 'library_return status=0' in result.stdout,result.stdout
   return captures[-2].read_bytes().split(b'255\n',1)[1],result.stdout
  def body(p):return p[32*684:1144*684]
  plain,_=run('plain','')
  cancelled,out=run('cancel','styles,tap:500:970,tap:312:220,style-preview,style-form,style-cancel,')
  assert 'style_ui command=65 status=0' in out and body(cancelled)==body(plain) and not list(state.glob('*.style.*')),out
  paper,out=run('apply','styles,tap:500:970,tap:312:220,style-apply,')
  assert body(paper)!=body(plain) and all(b==0 for a,b in zip(body(plain),body(paper)) if a==0),out
  payload=max((p.read_bytes() for p in state.glob('*.style.*')),key=lambda b:int.from_bytes(b[8:16],'little'));at=payload.index(b'PNTS')
  assert struct.unpack_from('<HH',payload,at+4)==(7,1)
  reopened,_=run('reopen','');assert body(reopened)==body(paper)
  flipped,_=run('turn','next,wait:300,previous,');assert body(flipped)==body(paper)
  none,_=run('none','styles,tap:500:970,tap:100:220,style-apply,');assert body(none)==body(plain)
  print(kind,'paper: preview cancellation, applied schema7, opaque black glyphs, reopen, next/previous and none passed')
