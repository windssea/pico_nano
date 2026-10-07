"""实际ZIP正文流/锚点/CRC及故障检查。 / Real ZIP body streams, anchors, CRC and failure checks."""
import json
import os
import pathlib
import re
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib

binary=sys.argv[1]
body='<html xmlns="http://www.w3.org/1999/xhtml"><body><p id="start"> A  中<b>B&amp;C</b> D</p><script>ignore()</script><p hidden="hidden">不显示</p><pre>x\n y</pre></body></html>'
with tempfile.TemporaryDirectory() as folder:
    path=pathlib.Path(folder)/'book.epub'
    def write(content=body,css=None):
        with zipfile.ZipFile(path,'w',compression=zipfile.ZIP_DEFLATED) as z:
            z.writestr('mimetype',b'application/epub+zip',compress_type=zipfile.ZIP_STORED)
            z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
            z.writestr('OPS/book.opf','<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="2.0" unique-identifier="uid"><metadata><d:title>test</d:title><d:identifier id="uid">test</d:identifier></metadata><manifest><item id="one" href="a.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="one"/></spine></package>')
            if css is not None:
                content=content.replace('<body>','<head><link rel="stylesheet" href="sheet.css"/></head><body>')
                z.writestr('OPS/sheet.css',css,compress_type=zipfile.ZIP_STORED)
            z.writestr('OPS/a.xhtml',content,compress_type=zipfile.ZIP_STORED)
    def run(success=True, anchor=None, env=None):
        command=[binary,str(path)]+(['OPS/a.xhtml',anchor] if anchor else [])
        r=subprocess.run(command,capture_output=True,timeout=20,env=dict(os.environ,**(env or {})))
        assert (r.returncode==0)==success,r.stderr
        assert b'used=0 live=0' in r.stderr,r.stderr
        if not success:assert not r.stdout
        return r
    write();r=run();data=json.loads(r.stdout)[0];expected='A 中B&C Dx\n y'
    assert data['characters']==len(expected) and data['utf32le_crc32']==zlib.crc32(expected.encode('utf-32-le'))
    assert json.loads(run(anchor='start').stdout)=={'element':3,'run':0,'offset':0,'kind':0}
    run(False,anchor='absent')
    attempts=int(re.search(rb'attempts=(\d+)',r.stderr)[1])
    for fault in range(1,attempts+1):run(False,env={'PN_BODY_FAIL_AT':str(fault)})
    styled=body.replace('id="start"','id="start" class="hide"')
    write(styled,css='.hide {display:none} script {display:block}')
    r=run();data=json.loads(r.stdout)[0]
    assert data['characters']==len('x\n y') and data['utf32le_crc32']==zlib.crc32('x\n y'.encode('utf-32-le'))
    run(False,anchor='start')
    attempts=int(re.search(rb'attempts=(\d+)',r.stderr)[1])
    for fault in range(1,attempts+1):run(False,env={'PN_BODY_FAIL_AT':str(fault)})
    for href,status in [('missing.css',10),('https://example.invalid/sheet.css',9),('../../escape.css',9),('sheet.css?x=1',9)]:
        content=body.replace('<body>','<head><link rel="stylesheet" href="'+href+'"/></head><body>')
        write(content);r=run(False);assert f'body status={status} '.encode() in r.stderr,r.stderr
    for text,status in [('img {width:1px',10),(' ' * 65537,4),('img{width:1px}' * 513,4),('img{width:1px}\0',10)]:
        write(body,css=text);r=run(False);assert f'body status={status} '.encode() in r.stderr,r.stderr
    for copies,text in [(17,' '),(3,' '*50000)]:
        head='<head>'+('<style>'+text+'</style>')*copies+'</head>'
        write(body.replace('<body>',head+'<body>'));r=run(False);assert b'body status=4 ' in r.stderr,r.stderr
    write(body,css=' '*10000+'.hide{display:none}');r=run()
    attempts=int(re.search(rb'attempts=(\d+)',r.stderr)[1])
    # 长源缓冲的增长分配也逐点故障注入。/ Fault-inject every observed allocation including large source-buffer growth.
    for fault in range(1,attempts+1):run(False,env={'PN_BODY_FAIL_AT':str(fault)})
    write(body.encode('utf-16'));run()
    write(body.replace('<pre>','<pre id="start">'));run(False,anchor='start')
    write(body.replace('id="start"','id="start" hidden="hidden"'));run(False,anchor='start')
    write()
    with zipfile.ZipFile(path) as z:offset=z.getinfo('OPS/a.xhtml').header_offset
    raw=bytearray(path.read_bytes());names,extra=struct.unpack_from('<HH',raw,offset+26);begin=offset+30+names+extra
    at=raw.index(b'> A ',begin);raw[at+2]=ord('Z');path.write_bytes(raw);run(False)
    print('body: real resources, stable anchors, missing/duplicate/hidden IDs, CRC and allocation faults passed')
