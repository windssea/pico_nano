"""壁纸设置页真实目录驱动。 / Wallpaper settings page driven with real directories."""
import pathlib,shutil,struct,subprocess,sys,tempfile,zlib
binary,fixtures=sys.argv[1],pathlib.Path(sys.argv[2])

def png(width,height,pixel):
    raw=b''.join(b'\0'+bytes(pixel(x,y) for x in range(width)) for y in range(height))
    chunk=lambda kind,data:struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,0,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')

with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder);images=root/'wallpapers';images.mkdir();state=root/'internal';state.mkdir()
    (images/'a-fake.jpg').write_bytes(b'this is not a picture, only text bytes')
    (images/'b-photo.png').write_bytes(png(300,200,lambda x,y:(x*255)//300 if y<100 else 40))
    shutil.copy(fixtures/'jpeg-baseline.jpg',images/'c-small.jpg')
    (images/'notes.txt').write_text('ignored')
    r=subprocess.run([binary,str(images),str(state),str(root/'preview.pgm')],capture_output=True,timeout=600)
    assert r.returncode==0,(r.stdout,r.stderr)
    data=(root/'preview.pgm').read_bytes();assert data.startswith(b'P5\n684 1216\n15\n')
    print(r.stdout.decode().strip())
print('wallpaper ui: list/preview/cover/shift/rotate/hint/apply/cancel/busy/no-partition/missing-dir/fault-injected open passed')
