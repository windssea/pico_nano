"""封面提取与书架队列：EPUB声明/候选、TXT侧车、损坏与缺失。 / Cover extraction and shelf queue: EPUB declared/candidate, TXT sidecars, corrupt and missing."""
import os,pathlib,struct,subprocess,sys,tempfile,zipfile,zlib
binary=sys.argv[1];fixtures=pathlib.Path(sys.argv[2])
OK,EMPTY,UNSUPPORTED,CORRUPT=0,3,9,10

def png(width,height,pixel):
    """独立生成8位灰度PNG。/ Independently encode an 8-bit grayscale PNG."""
    raw=b''.join(b'\0'+bytes(pixel(x,y) for x in range(width)) for y in range(height))
    chunk=lambda kind,data:struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,0,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')

def epub(path,cover=None,name='cover.png',declare='epub3',stored=False):
    opf_items='<item id="a" href="a.xhtml" media-type="application/xhtml+xml"/>'
    meta=''
    if cover is not None:
        props=' properties="cover-image"' if declare=='epub3' else ''
        kind='image/jpeg' if name.endswith('.jpg') else 'image/png'
        opf_items+=f'<item id="c" href="{name}" media-type="{kind}"{props}/>'
        if declare=='epub2':meta='<meta name="cover" content="c"/>'
    version='3.0' if declare=='epub3' else '2.0'
    with zipfile.ZipFile(path,'w',compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
        z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('OPS/book.opf',f'<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="{version}" unique-identifier="uid"><metadata><d:title>封面测试</d:title><d:identifier id="uid">cover</d:identifier>{meta}</metadata><manifest>{opf_items}</manifest><spine><itemref idref="a"/></spine></package>')
        z.writestr('OPS/a.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><body><p>正文</p></body></html>')
        if cover is not None:z.writestr('OPS/'+name,cover,compress_type=zipfile.ZIP_STORED if stored else zipfile.ZIP_DEFLATED)

W,H=184,256 # 网格封面尺寸，与PN_COVER_WIDTH/HEIGHT一致 / Grid cover size, matching PN_COVER_WIDTH/HEIGHT
def pgm(path):
    data=path.read_bytes();header,_,rest=data.partition(b'\n15\n');w,h=map(int,header.split()[1:3]);assert len(rest)==w*h;return w,h,rest

def render(book,expected,out):
    r=subprocess.run([binary,'render',str(book),str(out)],capture_output=True,timeout=120)
    assert r.returncode==0,(book,r.stdout,r.stderr)
    status=int(r.stdout.split(b'status=')[1].split()[0]);assert status==expected,(book,status,expected,r.stdout)
    return pgm(out) if expected==OK else None

with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder);books=root/'books';books.mkdir()
    halves=png(170,110,lambda x,y:0 if x<85 else 255)
    epub(books/'a-declared.epub',halves)
    epub(books/'b-epub2-jpeg.epub',(fixtures/'jpeg-progressive.jpg').read_bytes(),'art.jpg','epub2')
    epub(books/'c-candidate.epub',halves,'cover.png',declare='none')
    epub(books/'d-none.epub')
    bad=bytearray(halves);bad[60]^=0xff;epub(books/'e-corrupt.epub',bytes(bad),stored=True)
    (books/'f-sidecar.txt').write_text('正文','utf-8');(books/'f-sidecar.cover.png').write_bytes(png(40,200,lambda x,y:96))
    (books/'g-plain.txt').write_text('正文','utf-8')
    (books/'h-garbage.txt').write_text('正文','utf-8');(books/'h-garbage.jpg').write_bytes(b'not an image at all')

    # 宽图contain：上下留白，左黑右白；超采样均值不产生越界灰。/ Wide contain: white bands, left black, right white.
    w,h,p=render(books/'a-declared.epub',OK,root/'a.pgm');assert (w,h)==(W,H)
    band=(H-110*W//170)//2
    assert all(p[y*W+x]==15 for y in range(band-1) for x in range(W)),'top band white'
    assert all(p[y*W+x]==0 for y in range(band+2,H-band-2) for x in range(W//2-4)),'left black'
    assert all(p[y*W+x]==15 for y in range(band+2,H-band-2) for x in range(W//2+4,W)),'right white'
    w,h,p=render(books/'b-epub2-jpeg.epub',OK,root/'b.pgm');assert len(set(p))>2,'gradient survives'
    render(books/'c-candidate.epub',OK,root/'c.pgm')
    render(books/'d-none.epub',EMPTY,root/'d.pgm')
    render(books/'e-corrupt.epub',CORRUPT,root/'e.pgm')
    # 高图contain：左右留白，中间灰96→4bpp 6。/ Tall contain: side bands, gray 96 → 4bpp 6.
    w,h,p=render(books/'f-sidecar.txt',OK,root/'f.pgm');side=(W-40*H//200)//2
    inner=[p[y*W+x] for y in range(H) for x in range(W//2-5,W//2+5)];assert set(inner)=={6},set(inner)
    assert all(p[y*W+x]==15 for y in range(H) for x in range(side-1)),'left band white'
    render(books/'g-plain.txt',EMPTY,root/'g.pgm')
    render(books/'h-garbage.txt',UNSUPPORTED,root/'h.pgm')

    def queue(cache='-'):
        r=subprocess.run([binary,'queue',str(books),str(root/'shelf.pgm'),str(cache)],capture_output=True,timeout=300)
        assert r.returncode==0,(r.stdout,r.stderr)
        return {line.split()[0]:(int(line.split('state=')[1].split()[0]),int(line.split('reason=')[1])) for line in r.stdout.decode().splitlines()}
    expected={'a-declared.epub':(2,OK),'b-epub2-jpeg.epub':(2,OK),'c-candidate.epub':(2,OK),'d-none.epub':(0,EMPTY),'e-corrupt.epub':(3,CORRUPT),'f-sidecar.txt':(2,OK)}
    states=queue();assert states==expected,states
    w,h,p=pgm(root/'shelf.pgm');assert (w,h)==(684,1216)
    # 第一行封面槽左半为黑，证明缩略图被绘入书架。/ Left half of the first slot is black, proving the thumbnail reached the shelf.
    assert p[(400+H//2)*684+32+10]==0 and p[(400+H//2)*684+32+W-14]==15
    plain=p

    # 缓存：首次写入全部六条内容结论，父目录按需创建，无临时残留。/ Cache: first run stores all six content outcomes, creating parents, leaving no temporaries.
    (root/'tf').mkdir();cache=root/'tf'/'.readpico'/'covers'
    assert queue(cache)==expected
    files=sorted(cache.iterdir());assert len(files)==6 and all(f.suffix=='.pnc' for f in files),files
    assert pgm(root/'shelf.pgm')[2]==plain,'cached path draws identically'
    # 命中：原书改成损坏内容但保持大小与mtime，仍显示缓存。/ Hit: corrupt the book while keeping size and mtime; the cache still serves.
    a=books/'a-declared.epub';st=a.stat();original=a.read_bytes()
    with zipfile.ZipFile(a) as z:info=z.getinfo('OPS/cover.png')
    target=info.header_offset+30+len(info.filename.encode())+len(info.extra)+info.compress_size//2
    data=bytearray(original);data[target]^=0xff;a.write_bytes(bytes(data));os.utime(a,ns=(st.st_atime_ns,st.st_mtime_ns))
    assert queue(cache)==expected
    # 失效：mtime改变后重新解码，发现封面损坏。/ Invalidation: a new mtime forces decode, which finds the damaged cover.
    os.utime(a,ns=(st.st_atime_ns,st.st_mtime_ns+5_000_000_000))
    assert queue(cache)['a-declared.epub']==(3,CORRUPT)
    a.write_bytes(original)
    # 损坏缓存文件：CRC拒绝后重建为正确结果。/ Corrupt cache file: CRC rejects it and the result is rebuilt correctly.
    for f in cache.iterdir():
        b=bytearray(f.read_bytes());b[len(b)-10]^=0x55;f.write_bytes(bytes(b))
    assert queue(cache)==expected
    assert pgm(root/'shelf.pgm')[2]==plain
    # TXT侧车删除改变键，转为无封面。/ Removing the TXT sidecar changes the key and yields no cover.
    (books/'f-sidecar.cover.png').unlink()
    states=queue(cache);assert states['f-sidecar.txt']==(0,EMPTY),states
    assert not any(f.name.endswith('.tmp') for f in cache.iterdir())
print('cover: EPUB declared/EPUB2/candidate/none/corrupt, TXT sidecar/none/garbage, fault-injected allocation, shelf queue and TF cache hit/invalidate/rebuild passed')
