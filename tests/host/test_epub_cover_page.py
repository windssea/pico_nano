"""独页封面的适配边界；公开小图只测策略，真实书籍另走私人矩阵。 / Standalone-cover fit boundaries; tiny public images test policy, real books use the private corpus."""
import os,pathlib,re,struct,subprocess,sys,tempfile,zipfile,zlib

def png(w,h):
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,0,0,0,0))+chunk(b'IDAT',zlib.compress((b'\0'+b'\0'*w)*h))+chunk(b'IEND',b'')

with tempfile.TemporaryDirectory() as temporary:
    root=pathlib.Path(temporary)
    for label,w,h,kind in [('portrait',16,24,'cover'),('landscape',32,16,'cover'),('tall',4,100,'cover'),('square',16,16,'cover'),('logo',16,24,'logo'),('inline',16,24,'inline'),('caption',16,24,'caption'),('multi',16,24,'multi'),('broken',16,24,'broken')]:
        book=root/(label+'.epub');target=root/(label+'.pgm');declared=kind!='logo'
        image='<img src="illustration.png" style="width:10px"/>'
        body='<p>A'+image+'B</p>' if kind=='inline' else image+'<p>Caption</p>' if kind=='caption' else image+image if kind=='multi' else image
        with zipfile.ZipFile(book,'w',compression=zipfile.ZIP_DEFLATED) as z:
            z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
            z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
            cover='<meta name="cover" content="im"/>' if declared else ''
            z.writestr('book.opf',f'<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="2.0" unique-identifier="uid"><metadata><d:title>Boundary</d:title><d:identifier id="uid">{label}</d:identifier>{cover}</metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/><item id="im" href="illustration.png" media-type="image/png"/></manifest><spine><itemref idref="a"/></spine></package>')
            z.writestr('a.xhtml',f'<html xmlns="http://www.w3.org/1999/xhtml"><body>{body}</body></html>')
            z.writestr('illustration.png',b'broken' if kind=='broken' else png(w,h))
        result=subprocess.run([sys.argv[1],str(book),'', '1',str(target),'44'],env=dict(os.environ,PN_EPUB_IMAGE_RECTS='1'),capture_output=True,text=True,timeout=45)
        assert result.returncode==0 and 'used=0 live=0' in result.stderr,(label,result.stdout,result.stderr[-2500:])
        if kind=='broken':continue
        matches=re.findall(r'image_rect path=.*? x=(\d+) y=(\d+) w=(\d+) h=(\d+) viewport_w=(\d+) viewport_h=(\d+) native_w=(\d+) native_h=(\d+) status=0',result.stderr);assert matches,(label,result.stderr)
        x,y,rw,rh,vw,vh,nw,nh=map(int,matches[0]);assert (nw,nh)==(w,h)
        if kind!='cover':assert rw==10 and rh==15,(label,matches);continue
        ew=vw;eh=h*ew//w
        if eh>vh:eh=vh;ew=w*eh//h
        assert (x,y,rw,rh)==((vw-ew)//2,(vh-eh)//2,ew,eh),(label,matches)
        raw=target.read_bytes().split(b'\n',3);assert raw[:3]==[b'P5',b'684 1216',b'255']
        positions=[(i%684,i//684+32) for i,value in enumerate(raw[3][32*684:1144*684]) if value!=255]
        assert positions
        bounds=min(p[0] for p in positions),min(p[1] for p in positions),max(p[0] for p in positions)+1,max(p[1] for p in positions)+1
        left=(684-vw)//2+x;top=32+y
        assert bounds==(left,top,left+rw,top+rh),(label,bounds,matches)
        print('PASS cover fit',label,(x,y,rw,rh),flush=True)
print('Standalone cover: portrait/landscape/tall/square pixels, inline/logo/caption/multi guards and broken-image fallback passed')
