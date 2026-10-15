"""公开的槽重排/来源变化边界，不作真实书籍兼容样本。 / Public slot/source-boundary fixtures, not real-book compatibility samples."""
import os,pathlib,struct,subprocess,sys,tempfile,zlib

def png(seed):
    raw=b''.join(b'\0'+bytes((x*15+y*7+seed)%256 for x in range(8)) for y in range(12))
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',8,12,8,0,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')

for count in (1,2,13):
    with tempfile.TemporaryDirectory() as folder:
        root=pathlib.Path(folder);cache=root/'cache';cache.mkdir();books=[]
        for index in range(count):
            path=root/f'boundary-{index}.txt';path.write_text('Boundary fixture.\n',encoding='utf8');path.with_suffix('.png').write_bytes(png(index*19));books.append(path)
        r=subprocess.run([sys.argv[1],str(cache),*map(str,books)],capture_output=True,text=True,timeout=120)
        assert r.returncode==0,(r.stdout[-2000:],r.stderr[-3000:])
        assert r.stdout.count('exact_frames=1')==41 and 'used=0 live=0 cancellation=1 usb_fence=1 source_change=1' in r.stdout,r.stdout
        print(r.stdout)
        # 另走实际延后写入任务，确认离页后返回仍完整落盘。/ Exercise the real deferred writer separately and require complete persistence after leaving and returning.
        before,after=root/'before',root/'after'
        written=subprocess.run([sys.argv[2],str(before),str(after),*map(str,books[:12])],env=dict(os.environ,PN_COVER_REBIND_BEFORE_WRITE='1'),capture_output=True,text=True,timeout=120)
        assert written.returncode==0 and 'resident_rebind_before_write=1 offpage_resume=1' in written.stdout,(written.stdout[-2000:],written.stderr[-3000:])
        original={p.name:p.read_bytes() for p in before.glob('*.pnc')};actual={p.name:p.read_bytes() for p in after.glob('*.pnc')}
        assert len(original)==min(count,12) and original==actual
