"""SDL书架空闲封面队列与显式缓存目录。 / SDL shelf idle cover queue with an explicit cache directory."""
import os,pathlib,struct,subprocess,sys,tempfile,zipfile,zlib

def png(width,height,value):
    raw=b''.join(b'\0'+bytes([value])*width for _ in range(height))
    chunk=lambda kind,data:struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,0,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')

with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder);books=root/'books';books.mkdir();state=root/'state';(root/'tf').mkdir();cache=root/'tf'/'.readpico'/'covers'
    with zipfile.ZipFile(books/'a.epub','w',compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
        z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('OPS/book.opf','<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="3.0" unique-identifier="uid"><metadata><d:title>封面</d:title><d:identifier id="uid">c</d:identifier></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/><item id="c" href="c.png" media-type="image/png" properties="cover-image"/></manifest><spine><itemref idref="a"/></spine></package>')
        z.writestr('OPS/a.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><body><p>正文</p></body></html>')
        z.writestr('OPS/c.png',png(120,180,40))
    (books/'b.txt').write_text('阅读测试。\n'*50,encoding='utf-8');(books/'b.cover.png').write_bytes(png(60,90,200))
    (books/'c.txt').write_text('无封面。\n',encoding='utf-8')

    def run(script,*extra):
        r=subprocess.run([sys.argv[1],'--library',str(books),'--state-dir',str(state),*extra],env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT=script),capture_output=True,text=True,timeout=60)
        assert r.returncode==0,(r.stdout,r.stderr)
        assert 'used=0 live=0' in r.stdout,r.stdout
        return r.stdout

    # 默认不写缓存：空闲解码两张封面后一次重绘。/ No cache by default: two covers decode during idle, then one redraw.
    out=run('wait:1500,quit');assert out.count('covers ready=2 status=0')==1,out
    assert not any((root/'tf').iterdir())
    # 显式缓存：首次解码并写入三条结论；再次启动全部来自缓存，无空闲重绘。/ Explicit cache: first run decodes and stores three outcomes; restart serves all from cache with no idle redraw.
    out=run('wait:1500,quit','--cover-cache',str(cache));assert out.count('covers ready=2 status=0')==1,out
    assert len(list(cache.glob('*.pnc')))==3,list(cache.iterdir())
    out=run('wait:1000,quit','--cover-cache',str(cache));assert 'covers ready' not in out,out
    # 选项只用于书库窗口。/ The option is valid for the library window only.
    assert subprocess.run([sys.argv[1],'--cover-cache',str(cache)],env=dict(os.environ,SDL_VIDEODRIVER='dummy'),capture_output=True).returncode==2
print('cover window: idle queue, single redraw, explicit cache store and restart hit passed')
