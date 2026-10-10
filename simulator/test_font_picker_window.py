"""真实字体选择/原文预览/取消/应用及重开。 / Actual font selection/anchored preview/cancel/apply/reopen."""
import pathlib,tempfile,subprocess,os,sys,zipfile,shutil,struct
repo=pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as directory:
 root=pathlib.Path(directory);books=root/'books';fonts=root/'fonts';books.mkdir();fonts.mkdir();primary=fonts/'a.ttf';shutil.copyfile(repo/'assets/fonts/read-pico-ui.ttf',primary);shutil.copyfile(repo/'tests/fixtures/font-fallback.ttf',fonts/'b.ttf')
 (books/'a.txt').write_text('字体选择与阅读预览保持原文位置。ABCDEFGHIJKLMNOPQRSTUVWXYZ\n'*200,encoding='utf-8')
 epub=books/'a.epub'
 with zipfile.ZipFile(epub,'w',compression=zipfile.ZIP_DEFLATED) as z:
  z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
  z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
  z.writestr('OPS/book.opf','<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="2.0" unique-identifier="uid"><metadata><d:title>字体选择</d:title><d:identifier id="uid">picker</d:identifier></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="a"/></spine></package>')
  z.writestr('OPS/a.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><body>'+'<p>字体选择与阅读预览保持原文位置。ABCDEFGHIJKLMNOPQRSTUVWXYZ</p>'*200+'</body></html>')
 for kind in ('txt','epub','library'):
  mode='--library' if kind=='library' else '--book';path=books if kind=='library' else books/('a.'+kind);state=root/(kind+'-state');prefix='enter,' if kind=='library' else '';suffix='back,quit' if kind=='library' else 'quit'
  def run(script):
   env=dict(os.environ,SDL_VIDEODRIVER='dummy',**{('PN_SIM_LIBRARY_SCRIPT' if kind=='library' else 'PN_SIM_INPUT_SCRIPT'):prefix+script+suffix})
   r=subprocess.run([sys.argv[1],mode,str(path),'--font',str(primary),'--state-dir',str(state)],capture_output=True,text=True,env=env,timeout=45)
   assert r.returncode==0 and 'used=0 live=0' in r.stdout,(r.stdout,r.stderr)
   return r
  r=run('styles,tap:100:570,release:200:180,font-select,font-preview,font-form,font-back,font-cancel,style-cancel,')
  assert 'font_ui command=34 status=0' in r.stdout and 'font_ui command=7 status=0' in r.stdout and 'font_ui command=9 status=0 active=0' in r.stdout,r.stdout
  assert not list(state.glob('*.fonts.*'))
  r=run('styles,fonts,font-fallback,font-down,font-select,font-preview,font-form,font-apply,style-cancel,')
  assert 'font_ui command=8 status=0 active=0' in r.stdout,r.stdout
  snapshots=list(state.glob('*.fonts.*'));assert snapshots
  payload=snapshots[0].read_bytes();at=payload.index(b'PNFP');assert struct.unpack_from('<H',payload,at+40)[0]==1 and struct.unpack_from('<H',payload,at+88)[0]==1
  r=run('styles,fonts,font-cancel,style-cancel,');assert 'font_ui open status=0 active=1' in r.stdout,r.stdout
  before={p.name:p.read_bytes() for p in snapshots}
  r=run('styles,fonts,font-primary,font-select,font-default,font-back,font-cancel,style-cancel,')
  assert 'font_ui command=12 status=0' in r.stdout,r.stdout
  assert {p.name:p.read_bytes() for p in state.glob('*.fonts.*')}==before
  assert list(state.glob('fonts.*'))
  r=run('styles,fonts,font-inherit,style-cancel,')
  assert 'font_ui command=13 status=0 active=0' in r.stdout,r.stdout
  snapshots=list(state.glob('*.fonts.*'));newest=max(snapshots,key=lambda p:struct.unpack_from('<Q',p.read_bytes(),12)[0]);payload=newest.read_bytes();at=payload.index(b'PNFP')
  assert struct.unpack_from('<H',payload,at+6)[0]&2 and struct.unpack_from('<H',payload,at+40)[0]==0 and struct.unpack_from('<H',payload,at+88)[0]==0

print('Font picker SDL: TXT/EPUB/library pointer entry, ignored release, original preview/cancel, fallback apply and durable reopen passed')
