"""原生EPUB窗口翻页、字号、点击与跨进程恢复。 / Native EPUB window turns, size, clicks and process restore."""
import os,pathlib,subprocess,sys,tempfile,zipfile,re
with tempfile.TemporaryDirectory() as folder:
 root=pathlib.Path(folder);book=root/'book.epub';state=root/'state'
 with zipfile.ZipFile(book,'w',compression=zipfile.ZIP_DEFLATED) as z:
  z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
  z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
  z.writestr('OPS/book.opf','<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="2.0" unique-identifier="uid"><metadata><d:title>阅读测试</d:title><d:identifier id="uid">test</d:identifier></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="a"/></spine></package>')
  body='<h2>阅读测试</h2>'+''.join(f'<p>阅读位置 {i:04d}，窗口翻页和字号改变保留原文位置。</p>' for i in range(200))
  z.writestr('OPS/a.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><body>'+body+'</body></html>')
 def run(script):
  env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_INPUT_SCRIPT=script)
  r=subprocess.run([sys.argv[1],'--book',str(book),'--state-dir',str(state)],env=env,capture_output=True,text=True,timeout=40)
  assert r.returncode==0,(r.stdout,r.stderr)
  assert 'used=0 live=0' in r.stdout and 'epub_close status=0' in r.stdout,r.stdout
  events=re.findall(r'epub_event action=(\w+) status=(\d+) path=(\S+) element=(\d+) run=(\d+) offset=(\d+)',r.stdout)
  assert events and all(int(e[1])==0 for e in events),r.stdout
  return events
 events=run('next,next,previous,larger,smaller,tap:320:1160,quit')
 assert [e[0] for e in events]==['open','next','next','previous','larger','smaller','next']
 assert events[1][2:]==events[3][2:]==events[4][2:]==events[5][2:]
 assert events[6][2:]!=events[5][2:]
 reopened=run('quit');assert len(reopened)==1 and reopened[0][2:]==events[-1][2:]
print('EPUB SDL: actual display frames, key/pointer turns, type size, save-close and process reopen passed')
