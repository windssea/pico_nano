"""独立PNG夹具：同尺寸/长度的不同灰度与透明度。 / Independent PNG fixtures with equal dimensions/length and different gray/alpha."""
import pathlib,struct,subprocess,sys,tempfile,zlib
def png(seed):
    w,h=300,200
    raw=b''.join(b'\0'+b''.join(bytes(((x+seed)%256,(y+seed)%256)) for x in range(w)) for y in range(h))
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,4,0,0,0))+chunk(b'IDAT',zlib.compress(raw,0))+chunk(b'IEND',b'')
with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder);images=root/'images';images.mkdir();a=images/'a.png';b=images/'b.png';a.write_bytes(png(0));b.write_bytes(png(61))
    result=subprocess.run([sys.argv[1],str(a),str(b),str(root/'cache')],capture_output=True,timeout=120)
    assert result.returncode==0,(result.stdout,result.stderr)
    print(result.stdout.decode().strip())
