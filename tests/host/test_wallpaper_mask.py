"""独立生成四档透明度与正文样本。/ Independently generate four alpha levels and a body fixture."""
import pathlib, struct, subprocess, sys, tempfile, zlib
def chunk(kind,data):
    return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder);image=root/'mask.png';book=root/'book.txt'
    row=b'\0'+b''.join(bytes([0,a])*171 for a in [0,85,170,255])
    image.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',684,1216,8,4,0,0,0))+chunk(b'IDAT',zlib.compress(row*1216))+chunk(b'IEND',b''))
    book.write_text('PicoNano text underneath PNG masks.\n'*100,encoding='utf-8')
    result=subprocess.run([sys.argv[1],str(image),str(root),str(book)],capture_output=True,timeout=30)
    assert result.returncode==0,(result.stdout,result.stderr)
    print(result.stdout.decode())
