"""真实同源字体的EPUB页面/衔接及失败输出。 / EPUB pages and continuity using the shared real font, including failed output."""
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import zipfile
import struct
import zlib
import binascii

binary=sys.argv[1];font=sys.argv[2]
with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder);book=root/'book.epub'
    def chunk(name,data):return struct.pack('>I',len(data))+name+data+struct.pack('>I',binascii.crc32(name+data))
    png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',16,16,8,6,0,0,0))+chunk(b'IDAT',zlib.compress((b'\0'+b'\0\0\0\xff'*16)*16))+chunk(b'IEND',b'')
    def write(malformed=False,with_image=False,bad_png=False,styled_image=False,css=None,head_css="",img_style="",img_attrs="",image_bytes=None):
        with zipfile.ZipFile(book,'w',compression=zipfile.ZIP_DEFLATED) as z:
            z.writestr('mimetype',b'application/epub+zip',compress_type=zipfile.ZIP_STORED)
            z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
            z.writestr('OPS/book.opf','<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="2.0" unique-identifier="uid"><metadata><d:title>test</d:title><d:identifier id="uid">test</d:identifier></metadata><manifest><item id="one" href="a.xhtml" media-type="application/xhtml+xml"/>'+('<item id="img" href="a.png" media-type="image/png"/>' if with_image else '')+'</manifest><spine><itemref idref="one"/></spine></package>')
            body='<html xmlns="http://www.w3.org/1999/xhtml"><body><h2>Read Pico</h2><p>'+('Read Pico keeps your place. '*300)+'</p></body></html>'
            if css is not None or head_css:
                head=('<link rel="stylesheet" href="sheet.css"/>' if css is not None else '')+('<style>'+head_css+'</style>' if head_css else '')
                body=body.replace('<body>','<head>'+head+'</head><body>')
                if css is not None:z.writestr('OPS/sheet.css',css,compress_type=zipfile.ZIP_STORED)
            if malformed:body=body.replace('</p></body>','</body>')
            if with_image:body=body.replace('<body>','<body>'+('<div style="text-align:right"><img style="width:40%" src="a.png"/></div>' if styled_image else '<img src="a.png"/>'));z.writestr('OPS/a.png',(png[:-1]+bytes([png[-1]^1]) if bad_png else png) if image_bytes is None else image_bytes)
            if css is not None or head_css:
                body=body.replace('<img ', '<img '+img_attrs+' style="'+img_style+'" ')
            z.writestr('OPS/a.xhtml',body)
    def run(page,size=44,success=True,env=None):
        capture=root/f'page-{page}-{size}.pgm'
        result=subprocess.run([binary,str(book),font,'0',str(page),str(size),str(capture)],capture_output=True,timeout=30,env=dict(os.environ,**(env or {})))
        assert (result.returncode==0)==success,result.stderr
        assert b'used=0 live=0' in result.stderr,result.stderr
        if success:
            raw=capture.read_bytes();assert raw.startswith(b'P5\n684 1216\n255\n')
            assert len(raw.split(b'\n',3)[3])==684*1216 and b'missing=0' in result.stdout
            return result.stdout,raw
        assert not capture.exists()
        return result
    write();first,raw1=run(1);second,raw2=run(2)
    assert raw1!=raw2
    next_anchor=re.search(rb'next=(\d+:\d+:\d+)',first)[1];begin=re.search(rb'begin=(\d+:\d+:\d+)',second)[1]
    assert next_anchor==begin
    run(1,56)
    run(3,44,False,{'PN_CAPTURE_BUDGET':'65536'})
    write(True);run(3,56,False)
    write(with_image=True);_,raw=run(1,32);pixels=raw.split(b'\n',3)[3];assert pixels[100*684+32]==0
    write(with_image=True,styled_image=True);_,raw=run(1,36);pixels=raw.split(b'\n',3)[3]
    # 620px的40%是248px，右对齐到x=404；逐像素检查实际绘制区域。
    # Forty percent of 620px is 248px, right aligned at x=404; check the actual drawn region pixel by pixel.
    for y in range(100,348):
        assert pixels[y*684+32:y*684+404]==b'\xff'*372
        assert pixels[y*684+404:y*684+652]==b'\0'*248
    write(with_image=True,css='div.logo {text-align:right} img.logo {width:40%}',img_attrs='class="logo"')
    # 外部类选择器负责图片尺寸；div负责可继承对齐。/ External class selector controls size; div controls inherited alignment.
    with zipfile.ZipFile(book) as z:entries={name:z.read(name) for name in z.namelist()}
    entries['OPS/a.xhtml']=entries['OPS/a.xhtml'].replace(b'<img ',b'<div class="logo"><img ').replace(b'/><h2>',b'/></div><h2>')
    with zipfile.ZipFile(book,'w') as z:
        for name,data in entries.items():z.writestr(name,data,compress_type=zipfile.ZIP_STORED if name=='mimetype' else zipfile.ZIP_DEFLATED)
    _,raw=run(1,38);pixels=raw.split(b'\n',3)[3]
    assert pixels[100*684+404:100*684+652]==b'\0'*248
    assert pixels[100*684+32:100*684+404]==b'\xff'*372
    # 每条独立核对最终像素宽度与级联优先级。/ Independently check final pixel width and cascade precedence.
    cases=[
        ('img {width:30px} .a {width:20px} img.a.b#target {width:10px}', '', '', 10),
        ('#target {width:12px} img {width:6px !important}', '', '', 6),
        ('#target {width:12px !important} img {width:6px}', '', 'width:4px', 12),
        ('#target {width:12px !important}', '', 'width:4px !important', 4),
        ('img {width:12px; width:bad !important}', 'img {width:7px}', '', 7),
        ('img,.a {width:11px} @media screen {img {width:3px}}', '', '', 11),
        ('div img, img {width:3px} img {width:8px}', '', '', 8),
        ('img.a {width:12px} img {width:7px}', '', '', 12),
        ('img {width:12px !important; width:7px}', '', '', 12),
        ('img {width:12px; /* ignored quote \" */ width:9px}', '', '', 9),
    ]
    for css,embedded,inline,width in cases:
        write(with_image=True,css=css,head_css=embedded,img_style=inline,img_attrs='id="target" class="b a"')
        _,raw=run(1,40);pixels=raw.split(b'\n',3)[3]
        assert pixels[100*684+32:100*684+32+width]==b'\0'*width
        assert pixels[100*684+32+width]==255,(css,width)
    write(with_image=True,css='img {width:12px}')
    with zipfile.ZipFile(book) as z:offset=z.getinfo('OPS/sheet.css').header_offset
    raw=bytearray(book.read_bytes());names,extra=struct.unpack_from('<HH',raw,offset+26)
    raw[offset+30+names+extra+5]^=1;book.write_bytes(raw)
    run(1,42,False)
    write(with_image=True,css=' ' * 65537);run(1,43,False)
    write(with_image=True,css='img{width:1px}' * 513);run(1,45,False)
    # 按内容识别JPEG，即使扩展名与OPF媒体声明仍为PNG。/ Detect JPEG content even when extension and OPF media type still say PNG.
    fixtures=pathlib.Path(font).parents[2]/'tests'/'fixtures'
    for index,name in enumerate(['jpeg-baseline.jpg','jpeg-progressive.jpg']):
        write(with_image=True,image_bytes=(fixtures/name).read_bytes());_,raw=run(1,46+index*2)
        pixels=raw.split(b'\n',3)[3];assert pixels[100*684+32]==0 and pixels[110*684+48]!=255
    write(with_image=True,image_bytes=(fixtures/'jpeg-progressive.jpg').read_bytes()[:-2]);run(1,50,False)
    write(with_image=True,image_bytes=(fixtures/'jpeg-progressive.jpg').read_bytes()+b'X');run(1,52,False)
    write(with_image=True,image_bytes=(fixtures/'jpeg-baseline.jpg').read_bytes())
    with zipfile.ZipFile(book) as z:entries={name:z.read(name) for name in z.namelist()}
    with zipfile.ZipFile(book,'w') as z:
        for name,data in entries.items():z.writestr(name,data,compress_type=zipfile.ZIP_STORED)
    with zipfile.ZipFile(book) as z:offset=z.getinfo('OPS/a.png').header_offset
    raw=bytearray(book.read_bytes());names,extra=struct.unpack_from('<HH',raw,offset+26)
    raw[offset+30+names+extra+13]^=1;book.write_bytes(raw);run(1,54,False)
    write(with_image=True)
    with zipfile.ZipFile(book) as z:entries={name:z.read(name) for name in z.namelist()}
    entries['OPS/a.xhtml']=entries['OPS/a.xhtml'].replace(b'src="a.png"',b'src="missing.png"')
    with zipfile.ZipFile(book,'w') as z:
        for name,data in entries.items():z.writestr(name,data,compress_type=zipfile.ZIP_STORED)
    assert b'capture status=10 ' in run(1,55,False).stderr
    write(with_image=True,bad_png=True);run(1,34,False)
    print('epub_capture: native-font pages, semantic continuity, budget and late malformed XML passed')
