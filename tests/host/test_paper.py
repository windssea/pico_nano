"""独立原生灰阶PNG与同源阅读背景回归。 / Independent native grayscale PNG and shared reading-paper regression."""
import hashlib
import os
import pathlib
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib

def png():
    def chunk(tag,data):
        return struct.pack('>I',len(data))+tag+data+struct.pack('>I',zlib.crc32(tag+data))
    row=bytes((((2*x//43)%16)<<4)|((2*x+1)//43)%16 for x in range(342))
    raw=b''.join(b'\x00'+row for _ in range(1216))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',684,1216,4,0,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')

with tempfile.TemporaryDirectory() as temporary:
    root=pathlib.Path(temporary);images=root/'backgrounds';images.mkdir();cache=root/'cache';cache.mkdir();state=root/'state';state.mkdir()
    for i in range(7):(images/f'{i:02}-paper.png').write_bytes(png())
    source=images/'00-paper.png';key=hashlib.sha256(str(source).encode()).hexdigest();native=cache/f'{key}-2.pni'
    book=root/'book.txt';text=('Reading paper keeps every native pixel and preserves the next page. '*30+'\n')*40;book.write_text(text,encoding='utf-8')
    epub=root/'book.epub'
    with zipfile.ZipFile(epub,'w') as z:
        z.writestr('mimetype','application/epub+zip');z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="content.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('content.opf','<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="id">paper</dc:identifier><dc:title>Paper</dc:title><dc:language>en</dc:language></metadata><manifest><item id="c" href="chapter.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="c"/></spine></package>')
        z.writestr('chapter.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Paper</title></head><body>'+''.join('<p>'+text.splitlines()[0]+'</p>' for _ in range(40))+'</body></html>')
    env={**os.environ,'PN_BACKGROUND_DIR':str(images),'PN_BACKGROUND_CACHE':str(cache)}
    subprocess.run([sys.argv[1],str(source),str(native),str(book),str(epub),str(state)],env=env,check=True)
