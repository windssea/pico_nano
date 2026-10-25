"""原创封面夹具与不可变端口观察器。 / Original cover fixture and immutable-port observer."""
import pathlib, struct, subprocess, sys, tempfile, zlib
with tempfile.TemporaryDirectory() as tmp:
    root = pathlib.Path(tmp); cache = root/'cache'; cache.mkdir()
    book = root/'book.txt'; book.write_text('Original cover fixture.\n'*200)
    def chunk(kind, data):
        return struct.pack('>I', len(data))+kind+data+struct.pack('>I', zlib.crc32(kind+data)&0xffffffff)
    rows = b''.join(b'\0'+bytes((x*13+y*7)&255 for x in range(184)) for y in range(256))
    book.with_suffix('.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR', struct.pack('>IIBBBBB',184,256,8,0,0,0,0))+chunk(b'IDAT', zlib.compress(rows))+chunk(b'IEND',b''))
    subprocess.run([sys.argv[1], str(cache), str(book)], check=True)
