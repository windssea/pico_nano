"""真实SDL热恢复、完整帧与设置失效；不代替面板计时。 / Actual SDL warm resume, full frames and settings invalidation, without panel timing claims."""
import os,pathlib,subprocess,sys,tempfile,zipfile
with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder)
    for kind in ('txt','epub'):
        books=root/kind;books.mkdir();text=''.join(f'第{i}段，阅读会话保持位置与完整像素。\n' for i in range(200))
        if kind=='txt':(books/'a.txt').write_text(text,encoding='utf8')
        else:
            with zipfile.ZipFile(books/'a.epub','w',compression=zipfile.ZIP_DEFLATED) as z:
                z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
                z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
                z.writestr('book.opf','<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="2.0" unique-identifier="u"><metadata><d:title>Original hot session</d:title><d:identifier id="u">pn-hot</d:identifier></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="a"/></spine></package>')
                z.writestr('a.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><body>'+''.join('<p>'+line+'</p>' for line in text.splitlines())+'</body></html>')
        def run(script,name):
            frames=root/(kind+'-'+name);frames.mkdir()
            env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT='library,'+script,PN_SIM_CLOCK='12:34',PN_SIM_FRAME_DIR=str(frames))
            r=subprocess.run([sys.argv[1],'--library',str(books),'--state-dir',str(root/(kind+'-state-'+name))],env=env,capture_output=True,text=True,timeout=60)
            assert r.returncode==0 and 'used=0 live=0' in r.stdout,(r.stdout,r.stderr)
            return r.stdout,sorted(frames.glob('frame-*.pgm'))
        out,frames=run('enter,next,back,enter,quit,quit','hit')
        assert out.count('library_hot hit=')==1 and out.count('library_open prepare')==0,out
        assert frames[-2].read_bytes()==frames[-4].read_bytes(),(out,frames)
        out,_=run('enter,next,back,settings,back,enter,quit,quit','settings')
        assert 'library_hot hit=' not in out and out.count('library_open name=')==2,out
print('hot window: TXT/EPUB saved position, full pixel reuse, settings invalidation and zero resource leaks passed')
