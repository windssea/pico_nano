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
   env=dict(os.environ,SDL_VIDEODRIVER='dummy',**{('PN_SIM_LIBRARY_SCRIPT' if kind=='library' else 'PN_SIM_INPUT_SCRIPT'):(('library,' if kind=='library' else '')+prefix+script+suffix)})
   r=subprocess.run([sys.argv[1],mode,str(path),'--font',str(primary),'--state-dir',str(state)],capture_output=True,text=True,env=env,timeout=45)
   assert r.returncode==0 and 'used=0 live=0' in r.stdout,(r.stdout,r.stderr)
   return r
  r=run('styles,tap:100:480,release:200:180,font-select,font-preview,font-form,font-back,font-cancel,style-cancel,')
  assert 'font_ui command=34 status=0' in r.stdout and 'font_ui command=7 status=0' in r.stdout and 'font_ui command=9 status=0 active=0' in r.stdout,r.stdout
  assert not list(state.glob('*.fonts.*'))
  r=run('styles,fonts,font-primary,font-down,font-select,font-preview,font-form,font-apply,style-cancel,')
  assert 'font_ui command=8 status=0 active=0' in r.stdout,r.stdout
  snapshots=list(state.glob('*.fonts.*'));assert snapshots
  payload=snapshots[0].read_bytes();at=payload.index(b'PNFP');assert struct.unpack_from('<H',payload,at+40)[0]==1 and struct.unpack_from('<H',payload,at+88)[0]==0
  r=run('styles,fonts,font-cancel,style-cancel,');assert 'font_ui open status=0 active=1' in r.stdout,r.stdout
  before={p.name:p.read_bytes() for p in snapshots}
  r=run('styles,fonts,font-primary,font-select,font-default,font-back,font-cancel,style-cancel,')
  assert 'font_ui command=12 status=0' in r.stdout,r.stdout
  assert {p.name:p.read_bytes() for p in state.glob('*.fonts.*')}==before
  assert list(state.glob('fonts.*'))
  r=run('styles,fonts,font-inherit,style-cancel,')
  assert 'font_ui command=13 status=0 active=0' in r.stdout,r.stdout
  snapshots=list(state.glob('*.fonts.*'));newest=max(snapshots,key=lambda p:struct.unpack_from('<Q',p.read_bytes(),12)[0]);payload=newest.read_bytes();at=payload.index(b'PNFP')
  assert (struct.unpack_from('<H',payload,at+6)[0]&2) and struct.unpack_from('<H',payload,at+40)[0]==0 and struct.unpack_from('<H',payload,at+88)[0]==0
  r=run('styles,fonts,tap:100:190,style-cancel,')
  assert 'font_ui command=13 status=0 active=0' in r.stdout,r.stdout

 # 阅读默认在旧目录时，选择页仍须扫描--font-dir；第七项在第二页且必须真正保存它的路径。
 # Even with a reading default in an old directory, the picker scans --font-dir; the seventh item on page two must save its actual path.
 installed=root/'installed';installed.mkdir()
 for i in range(7):shutil.copyfile(primary,installed/f'installed{i}.ttf')
 state=root/'directory-state'
 script='library,enter,styles,fonts,font-next,tap:100:430,style-cancel,back,enter,styles,fonts,font-cancel,style-cancel,back,quit'
 result=subprocess.run([sys.argv[1],'--library',str(books),'--font',str(primary),'--font-dir',str(installed),'--state-dir',str(state)],env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT=script),capture_output=True,text=True,timeout=60)
 assert result.returncode==0 and 'used=0 live=0' in result.stdout,(result.stdout,result.stderr)
 assert 'font_ui command=2 status=0' in result.stdout and 'font_ui command=16 status=0 active=0' in result.stdout,result.stdout
 records=list(state.glob('*.fonts.*'));assert records and any(str(installed/'installed6.ttf').encode() in p.read_bytes() for p in records)

 # 每次选择/取消只呈现一张最终排版页，原文预览才允许出现正文帧。
 # Each selection/cancel presents one final typesetting page; only explicit original-text preview may show body frames.
 for kind in ('txt','epub'):
  single=root/('transition-'+kind);single.mkdir();shutil.copyfile(books/('a.'+kind),single/('a.'+kind))
  journeys=[('direct','tap:100:518',6),('default','tap:100:190',6),('cancel','font-cancel',6),('detail','font-select,font-preview,font-form,font-apply',9),('preview-cancel','font-select,font-preview,font-form,tap:100:920',9)]
  for name,commands,total in journeys:
   frames=root/(kind+'-'+name+'-frames');frames.mkdir();session=root/(kind+'-'+name+'-state')
   script='library,enter,styles,fonts,'+commands+',window-close'
   result=subprocess.run([sys.argv[1],'--library',str(single),'--font',str(primary),'--font-dir',str(fonts),'--state-dir',str(session)],env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT=script,PN_SIM_FRAME_DIR=str(frames)),capture_output=True,text=True,timeout=60)
   captures=sorted(frames.glob('*.pgm'))
   assert result.returncode==0 and 'used=0 live=0' in result.stdout,(result.stdout,result.stderr)
   # 关闭窗口会提交会话并绘制一次书库；之前必须恰好是最终排版页。/ Window close commits the session and paints Library once; the frame before it must be the final typesetting page.
   # 小TXT不刷loading，EPUB保留；字体选择仍只有预期最终页。/ Small TXT skips loading, EPUB retains it; font selection still presents only its final page.
   assert len(captures)==total+(1 if kind=='txt' else 2),(kind,name,len(captures),result.stdout)
   payload=captures[-2].read_bytes().split(b'\n',3)[3]
   assert payload[1140*684+100]==0,(kind,name,'final page must be typesetting Apply')
   if name=='direct':assert any(str(fonts/'b.ttf').encode() in record.read_bytes() for record in session.glob('*.fonts.*'))

print('Reading font picker: TXT/EPUB/library preview/cancel, external primary with automatic fallback, global inheritance and built-in choice, configured directory/page two and durable reopen passed')
