"""PC目录键盘/触屏入口、页选择和实际恢复。 / PC TOC keyboard/pointer entry, paging and actual restore."""
import pathlib,tempfile,zipfile,subprocess,sys,os,re
with tempfile.TemporaryDirectory() as folder:
 root=pathlib.Path(folder);books=root/'books';books.mkdir();book=books/'a.epub'
 with zipfile.ZipFile(book,'w',compression=zipfile.ZIP_DEFLATED) as z:
  z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
  z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
  z.writestr('OPS/book.opf','<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="2.0" unique-identifier="uid"><metadata><d:title>目录窗口测试</d:title><d:identifier id="uid">test</d:identifier></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/><item id="n" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest><spine toc="n"><itemref idref="a"/></spine></package>')
  nav=''.join(f'<navPoint id="n{i}"><navLabel><text>第{i}节 目录测试</text></navLabel><content src="a.xhtml#p{i}"/></navPoint>' for i in range(8))
  z.writestr('OPS/toc.ncx','<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1"><navMap>'+nav+'</navMap></ncx>')
  body=''.join(f'<h2 id="p{i}">第{i}节</h2>'+('<p>阅读文字，页面控制与目录选择测试。</p>'*30) for i in range(8))
  z.writestr('OPS/a.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><body>'+body+'</body></html>')
 def run(mode,state,script):
  env=dict(os.environ,SDL_VIDEODRIVER='dummy',**{('PN_SIM_INPUT_SCRIPT' if mode=='--book' else 'PN_SIM_LIBRARY_SCRIPT'):script})
  r=subprocess.run([sys.argv[1],mode,str(book if mode=='--book' else books),'--state-dir',str(state)],env=env,capture_output=True,text=True,timeout=40)
  assert r.returncode==0 and 'used=0 live=0' in r.stdout,(r.stdout,r.stderr)
  return r
 state=root/'single'
 r=run('--book',state,'toc,release:100:170,toc-next,toc-select,quit')
 assert 'toc_ui command=2 status=0 active=1' in r.stdout and 'toc_ui command=16 status=0 active=0' in r.stdout,r.stdout
 reopened=run('--book',state,'tap:580:40,back,quit')
 assert 'toc_ui command=0 status=0 active=1' in reopened.stdout and 'toc_ui command=1 status=0 active=0' in reopened.stdout,reopened.stdout
 first=re.search(r'epub_event action=open status=0 path=(\S+) element=(\d+) run=(\d+) offset=(\d+)',r.stdout).groups()
 second=re.search(r'epub_event action=open status=0 path=(\S+) element=(\d+) run=(\d+) offset=(\d+)',reopened.stdout).groups();assert first!=second
 state=root/'library'
 r=run('--library',state,'enter,tap:580:40,release:100:170,toc-next,toc-select,back,continue,back,quit')
 assert 'toc_ui command=2 status=0 active=1' in r.stdout and 'toc_ui command=16 status=0 active=0' in r.stdout,r.stdout
 locations=re.findall(r'epub_library_position path=(\S+) element=(\d+) run=(\d+) offset=(\d+)',r.stdout);assert len(locations)==2 and locations[0]!=locations[1],r.stdout
 print('TOC SDL: standalone/library keyboard and pointer menus, directory pages, actual jump/save/reopen passed')
