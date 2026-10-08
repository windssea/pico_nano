"""壁纸contain/cover/旋转/透明与A/B记录。 / Wallpaper contain/cover/rotation/transparency and A/B records."""
import pathlib,struct,subprocess,sys,tempfile,zlib
binary=sys.argv[1]
W,H=684,1216

def png(width,height,pixel,alpha=False):
    """独立编码8位灰度或灰度+alpha PNG。/ Independently encode 8-bit gray or gray+alpha PNG."""
    rows=[]
    for y in range(height):
        row=bytearray(b'\0')
        for x in range(width):
            v=pixel(x,y);row+=bytes(v) if alpha else bytes([v])
        rows.append(bytes(row))
    chunk=lambda kind,data:struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,4 if alpha else 0,0,0,0))+chunk(b'IDAT',zlib.compress(b''.join(rows)))+chunk(b'IEND',b'')

def pgm(path):
    data=path.read_bytes();header,_,rest=data.partition(b'\n15\n');assert header.split()[1:3]==[b'684',b'1216'];return rest

def run(image,fit,rotation,shift,out,expected=0):
    r=subprocess.run([binary,'prepare',str(image),str(fit),str(rotation),str(shift),str(out)],capture_output=True,timeout=600)
    assert r.returncode==0,(r.stdout,r.stderr)
    status=int(r.stdout.split(b'status=')[1].split()[0]);assert status==expected,(image,status)
    return pgm(out) if expected==0 else None

with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder)
    # 四象限：左上0、右上85、左下170、右下255 → 4bpp 0/5/10/15。/ Quadrants map to 4bpp 0/5/10/15.
    quad=root/'quad.png';quad.write_bytes(png(200,100,lambda x,y:(0 if x<100 else 85) if y<50 else (170 if x<100 else 255)))
    at=lambda p,x,y:p[y*W+x]
    p=run(quad,0,0,0,root/'c0.pgm');top=(H-342)//2
    assert at(p,100,10)==15 and at(p,100,H-10)==15,'contain bands white'
    assert (at(p,100,top+50),at(p,600,top+50),at(p,100,top+300),at(p,600,top+300))==(0,5,10,15)
    # 顺时针90°：原左上转到右上。/ Clockwise 90°: original top-left moves to top-right.
    p=run(quad,0,1,0,root/'c1.pgm');left=(W-608)//2
    assert at(p,5,600)==15 and at(p,W-5,600)==15
    assert (at(p,left+152,304),at(p,left+456,304),at(p,left+152,912),at(p,left+456,912))==(10,0,15,5)
    p=run(quad,0,2,0,root/'c2.pgm');assert (at(p,100,top+50),at(p,600,top+50),at(p,100,top+300),at(p,600,top+300))==(15,10,5,0)
    p=run(quad,0,3,0,root/'c3.pgm');assert (at(p,left+152,304),at(p,left+456,304),at(p,left+152,912),at(p,left+456,912))==(5,15,0,10)
    # 铺满：居中可见交界；-4贴左、+4贴右。/ Cover: centered shows the seam; -4 aligns left, +4 aligns right.
    p=run(quad,1,0,0,root/'f0.pgm');assert (at(p,100,300),at(p,600,300),at(p,100,900),at(p,600,900))==(0,5,10,15)
    p=run(quad,1,0,-4,root/'fl.pgm');assert (at(p,100,300),at(p,600,300))==(0,0)
    p=run(quad,1,0,4,root/'fr.pgm');assert (at(p,100,300),at(p,600,300))==(5,5)
    # 透明铺白。/ Transparency composites onto white.
    clear=root/'clear.png';clear.write_bytes(png(60,90,lambda x,y:(0,0) if x<30 else (0,255),alpha=True))
    p=run(clear,1,0,0,root/'t.pgm');assert at(p,100,600)==15 and at(p,600,600)==0
    # 大图走2倍超采样：黑白分界附近出现均值灰，两侧仍纯黑/纯白。/ Large source uses 2× supersampling: averaged gray near the seam, pure black/white on each side.
    big=root/'big.png';big.write_bytes(png(1400,2400,lambda x,y:0 if (x//7)%2==0 else 255))
    p=run(big,0,0,0,root/'big.pgm');values=set(p[600*W+50:600*W+600]);assert values-{0,15},'supersampling averages stripes'
    # 坏图与非图片。/ Corrupt and non-image sources.
    bad=bytearray(quad.read_bytes());bad[40]^=0xff;(root/'bad.png').write_bytes(bytes(bad));run(root/'bad.png',0,0,0,root/'x.pgm',expected=10)
    (root/'text.jpg').write_bytes(b'hello, this file is plain text');run(root/'text.jpg',0,0,0,root/'x.pgm',expected=9)
    # 记录与锁屏。/ Records and lock screens.
    (root/'wp').mkdir()
    for args in (['store',str(root/'wp')],['lock',str(root/'lock')]):
        r=subprocess.run([binary,*args],capture_output=True,timeout=120);assert r.returncode==0,(args,r.stdout,r.stderr)
    default=pgm(root/'lock-0.pgm');assert 0 in default and len(set(default))>2
print('wallpaper: contain/cover/shift, four rotations, transparency, supersampling, corrupt input, fault injection, A/B records and lock screens passed')
