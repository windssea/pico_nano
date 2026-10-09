"""SDL壁纸设置页：打开、预览、应用、取消与返回书架。 / SDL wallpaper page: open, preview, apply, cancel and back to the shelf."""
import os,pathlib,struct,subprocess,sys,tempfile,zlib

def png(width,height,value):
    raw=b''.join(b'\0'+bytes((value if x<width//2 else 255) for x in range(width)) for y in range(height))
    chunk=lambda kind,data:struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,0,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')

with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder);books=root/'books';books.mkdir();(books/'a.txt').write_text('阅读测试。\n'*20,encoding='utf-8')
    images=root/'wallpapers';images.mkdir();(images/'sea.png').write_bytes(png(240,400,30))
    store=root/'internal';store.mkdir()
    def run(script,*extra):
        r=subprocess.run([sys.argv[1],'--library',str(books),'--state-dir',str(root/'state'),*extra],env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT=script),capture_output=True,text=True,timeout=120)
        assert r.returncode==0,(r.stdout,r.stderr)
        assert 'used=0 live=0' in r.stdout,r.stdout
        return r.stdout
    opts=('--wallpaper-dir',str(images),'--wallpaper-store',str(store))
    # 选图→铺满→Enter应用→Esc返回书架。/ Choose image → cover → Enter apply → Esc back to shelf.
    out=run('wallpaper,tap:100:400,tap:100:800,enter,back,quit',*opts)
    assert 'wallpaper_ui open status=0' in out,out
    assert 'wallpaper_ui command=16 status=0 screen=1 active=1' in out,out
    assert 'wallpaper_ui command=6 status=0 screen=1 active=1' in out,out
    assert 'wallpaper_ui command=11 status=0 screen=0 active=1' in out,out
    assert 'wallpaper_ui command=1 status=0 screen=0 active=0' in out,out
    record=store/'lock.a';assert record.exists() and record.stat().st_size>415872,list(store.iterdir())
    # 预览后Esc取消不写新记录。/ Esc after previewing cancels without a new record.
    before=record.read_bytes();out=run('wallpaper,tap:330:240,back,back,quit',*opts)
    assert 'wallpaper_ui command=5 status=0 screen=1 active=1' in out and 'wallpaper_ui command=12 status=0 screen=0 active=1' in out,out
    assert record.read_bytes()==before and not (store/'lock.b').exists()
    # 无记录目录：应用返回不支持。/ Without a record directory, apply reports unsupported.
    out=run('wallpaper,tap:100:240,enter,back,back,quit','--wallpaper-dir',str(images))
    assert 'wallpaper_ui command=11 status=9 screen=1 active=1' in out,out
    # 未给目录时w键无效；选项须配合书库。/ Without the option the w key does nothing; options require the library mode.
    out=run('wallpaper,quit');assert 'wallpaper_ui' not in out,out
    assert subprocess.run([sys.argv[1],'--wallpaper-dir',str(images)],env=dict(os.environ,SDL_VIDEODRIVER='dummy'),capture_output=True).returncode==2
print('wallpaper window: open, preview, cover, apply record, cancel, no-store and option validation passed')
