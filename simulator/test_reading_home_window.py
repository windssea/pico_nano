"""独立首页真实阅读、元数据与四栏返回。 / Independent Home with real reading, metadata and four-tab returns."""
import os
import pathlib
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib

def png():
    # 原创灰阶三带封面，验证实际解码，不借占位。/ Original three-band cover verifies decoding instead of placeholders.
    raw=b''.join(b'\0'+bytes([40 if y<40 else 180 if y<100 else 70])*120 for y in range(180))
    def chunk(tag,data):return struct.pack('>I',len(data))+tag+data+struct.pack('>I',zlib.crc32(tag+data))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',120,180,8,0,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')

with tempfile.TemporaryDirectory() as directory:
    root=pathlib.Path(directory);books=root/'books';books.mkdir();state=root/'state';state.mkdir();cache=root/'cache';cache.mkdir()
    book=books/'01-file-name.epub'
    with zipfile.ZipFile(book,'w',compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
        z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('OPS/book.opf','<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="3.0" unique-identifier="id"><metadata><d:identifier id="id">home-test</d:identifier><d:title>Actual Publication</d:title><d:creator>Actual Author</d:creator></metadata><manifest><item id="text" href="text.xhtml" media-type="application/xhtml+xml"/><item id="cover" href="cover.png" media-type="image/png" properties="cover-image"/></manifest><spine><itemref idref="text"/></spine></package>')
        z.writestr('OPS/text.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><body>'+ '<p>A quiet page. Read Pico remembers the original position.</p>'*240 +'</body></html>')
        z.writestr('OPS/cover.png',png())
    (books/'02-another.txt').write_text('Another book.\n'*200,encoding='utf-8')
    original=book.read_bytes()
    def run(script,frame_name):
        capture=root/frame_name;capture.mkdir()
        result=subprocess.run([sys.argv[1],'--library',str(books),'--state-dir',str(state),'--cover-cache',str(cache)],env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT=script,PN_SIM_FRAME_DIR=str(capture)),capture_output=True,text=True,timeout=120)
        assert result.returncode==0 and 'used=0 live=0' in result.stdout,(result.stdout,result.stderr)
        return result.stdout,[p.read_bytes().split(b'\n',3)[3] for p in sorted(capture.glob('*.pgm'))]

    out,frames=run('tap:300:500,wait:1800,tap:70:420,next,back,home,wait:800,tap:300:540,next,back,tap:427:1150,wait:1500,tap:598:1150,tap:85:1150,tap:256:1150,tap:600:190,window-close','first')
    assert out.startswith('navigation root=home count=0'),out
    assert out.count('library_open name=01-file-name.epub')==2 and out.count('library_return status=0')==2,out
    assert 'navigation root=home count=1' in out and 'settings closed request=25' in out,out
    assert 'hub open status=0' in out and 'hub command=4 status=0' in out,out
    # 等待传书页期间不能被封面队列覆盖。/ Cover completion cannot overwrite Transfer while waiting.
    during=out.split('hub open status=0',1)[1].split('hub command=4',1)[0]
    assert 'navigation root=' not in during,during
    records=[p.read_bytes() for p in cache.glob('*.pnc')]
    assert any(b'Actual Publication\0' in b and b'Actual Author\0' in b for b in records)
    assert book.read_bytes()==original
    # 冷缓存封面完成后的图书封面确为灰阶带；副标题元数据有像素输出。/ The completed cold-cache cover has the decoded bands and visible subtitle pixels.
    assert any(f[290*684+100]<100 and 120<f[340*684+100]<230 for f in frames)
    out,warm=run('wait:800,tap:300:540,back,window-close','warm')
    assert out.startswith('navigation root=home count=1') and 'library_open name=01-file-name.epub' in out,out
    assert 'covers ready=' not in out,out
    assert book.read_bytes()==original and len(warm)>=3
    print('reading home: actual EPUB metadata/PNG cover, empty/start, continue/return, warm cache, four tabs and no background overwrite passed')
