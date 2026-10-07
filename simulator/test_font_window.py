"""真实窗口备用源绑定及回收。 / Actual-window fallback-source binding and cleanup."""
import pathlib,os,sys,tempfile,subprocess,zipfile
root_repo=pathlib.Path(__file__).resolve().parents[1]; primary=root_repo/'tests/fixtures/font-fallback.ttf';fallback=root_repo/'assets/fonts/read-pico-ui.ttf'
with tempfile.TemporaryDirectory() as folder:
 root=pathlib.Path(folder);books=root/'books';books.mkdir();txt=books/'b.txt';epub=books/'a.epub';txt.write_text('A 中文阅读设置，字体回退，继续阅读。\n'*150,encoding='utf-8')
 with zipfile.ZipFile(epub,'w',compression=zipfile.ZIP_DEFLATED) as z:
  z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
  z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
  z.writestr('OPS/book.opf','<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="2.0" unique-identifier="uid"><metadata><d:title>备用字体</d:title><d:identifier id="uid">fallback</d:identifier></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="a"/></spine></package>')
  z.writestr('OPS/a.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><body>'+'<p>A 中文阅读设置，字体回退，继续阅读。</p>'*150+'</body></html>')
 for mode,path,script in [('--book',txt,'next,styles,style-plus,style-preview,style-form,style-cancel,quit'),('--book',epub,'next,styles,style-plus,style-preview,style-form,style-cancel,quit'),('--library',books,'enter,next,back,quit')]:
  env=dict(os.environ,SDL_VIDEODRIVER='dummy',**{('PN_SIM_LIBRARY_SCRIPT' if mode=='--library' else 'PN_SIM_INPUT_SCRIPT'):script})
  r=subprocess.run([sys.argv[1],mode,str(path),'--font',str(primary),'--fallback-font',str(fallback),'--state-dir',str(root/(path.stem+'-state'))],env=env,capture_output=True,text=True,timeout=40)
  assert r.returncode==0 and 'used=0 live=0' in r.stdout,(r.stdout,r.stderr)
print('Font fallback SDL: TXT/EPUB/library sources, reflow preview/cancel, save-close and zero retained allocations passed')
