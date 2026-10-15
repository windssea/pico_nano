"""原创RGBA扫描线与渐进JPEG的任务生命周期。 / Job lifecycles for original RGBA scanlines and progressive JPEG."""
import pathlib,struct,subprocess,sys,tempfile,zlib
with tempfile.TemporaryDirectory() as folder:
    path=pathlib.Path(folder)/'original.png';w,h=128,513
    raw=b''.join(b'\0'+bytes(c for x in range(w) for c in ((x*17+y)%256,(x+y*3)%256,(y*7)%256,(x*7+y*11)%256)) for y in range(h))
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))
    subprocess.run([sys.argv[1],str(path),sys.argv[2]],check=True,timeout=120)
