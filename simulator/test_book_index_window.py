"""整书索引开关、等待首屏顺序、后台与重开。 / Whole-book mode, first-frame ordering, background and reopening."""
import os,pathlib,subprocess,sys,tempfile,shutil,zipfile
with tempfile.TemporaryDirectory(prefix="pn-book-index-window-") as folder:
 root=pathlib.Path(folder);books=root/'books';books.mkdir();(books/'a.txt').write_text('Whole book indexing preserves the visible page and exact typography.\n'*400,encoding='utf-8')
 def run(name,script,state=None,delay=0):
  state=state or root/name
  state.mkdir(parents=True,exist_ok=True)
  frames=state/'frames';frames.mkdir(exist_ok=True)
  env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_CLOCK='12:34',PN_SIM_PRESENT_DELAY_MS=str(delay),PN_READING_CACHE=str(root/'tf'),PN_SIM_LIBRARY_SCRIPT=script,PN_SIM_FRAME_DIR=str(frames))
  r=subprocess.run([sys.argv[1],'--library',str(books),'--state-dir',str(state)],env=env,capture_output=True,text=True,timeout=90)
  assert r.returncode==0 and 'used=0 live=0' in r.stdout,(r.stdout,r.stderr)
  assert 'shelf_open_failed' not in r.stdout,(r.stdout,r.stderr)
  # All pages, including the index loader, must initialize the canvas margins. / 包括索引等待页在内，所有页面必须初始化画布边缘。
  for frame in frames.glob('*.pgm'):
   pixels=frame.read_bytes().split(b'\n',3)[3]
   assert pixels[0]==255 and pixels[683]==255,(frame,pixels[0],pixels[683])
  return r.stdout,state
 out,state=run('wait','library,settings,tap:100:950,tap:400:750,back,back,enter,wait:1500,home,quit')
 assert 'settings command=49 status=0' in out and 'book_index wait begin' in out and 'book_index wait end status=0' in out,out
 assert out.index('book_index wait end status=0')<out.index('library_open name=a.txt'),out
 assert list((root/'tf'/'pages').glob('*.pni')) and list((state/'indexes').glob('*.pnrt')),out
 out,_=run('warm','library,enter,home,quit',state)
 assert 'library_open name=a.txt' in out and 'book_index wait begin' not in out,out
 # A distinct layout cache/state avoids reusing the completed waiting-mode table. / 独立缓存与状态避免后台测试借用已完成表。
 old=root/'tf';old.rename(root/'wait-tf')
 out,_=run('background','library,enter,wait:5000,home,quit')
 assert 'book_index wait begin' not in out and 'book_index background complete' in out,out
 assert out.index('library_open name=a.txt')<out.index('book_index background complete'),out
 # Cancel while the index loader is active; no unshown reading history is committed. / 索引等待页内取消，不提交未显示正文的阅读记录。
 cancel=root/'cancel';cancel.mkdir()
 for saved in state.glob('indexing.*'):shutil.copy2(saved,cancel/saved.name)
 (books/'a.txt').write_text('A large private fixture keeps compilation cancellable.\n'*20000,encoding='utf-8')
 out,_=run('cancel','library,race-open-home:1000,wait:300,quit',cancel,600)
 assert 'book_index wait begin' in out and 'library_open name=a.txt' not in out and 'opening end status=8' in out,out
 assert not (cancel/'recent.a').exists() and not (cancel/'recent.b').exists(),out
 # EPUB's asynchronous open must keep the input gate until indexing completes. / EPUB异步开书必须保持输入门直到整书编译完成。
 (books/'a.txt').rename(books/'b.txt')
 with zipfile.ZipFile(books/'a.epub','w',compression=zipfile.ZIP_DEFLATED) as z:
  z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
  z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
  z.writestr('OPS/book.opf','<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="2.0" unique-identifier="uid"><metadata><d:title>Index</d:title><d:identifier id="uid">index</d:identifier></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="a"/></spine></package>')
  z.writestr('OPS/a.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><body>'+''.join('<p>Continuous page boundaries retain exact layout.</p>' for i in range(200))+'</body></html>')
 out,_=run('epub-wait','library,settings,tap:100:950,tap:400:750,back,back,enter,home,quit')
 assert 'book_index wait end status=0' in out and out.index('book_index wait end status=0')<out.index('library_open name=a.epub'),out
 print('book index window: saved option, first-frame waiting gate, trusted warm reuse, background order and no leaks passed')
