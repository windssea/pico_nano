"""SDL书架空闲封面队列与显式缓存目录。 / SDL shelf idle cover queue with an explicit cache directory."""
import os,pathlib,re,signal,struct,subprocess,sys,tempfile,threading,zipfile,zlib

def png(width,height,value):
    raw=b''.join(b'\0'+bytes([value])*width for _ in range(height))
    chunk=lambda kind,data:struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,0,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')

with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder);books=root/'books';books.mkdir();state=root/'state';(root/'tf').mkdir();cache=root/'tf'/'.readpico'/'covers'
    with zipfile.ZipFile(books/'a.epub','w',compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
        z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('OPS/book.opf','<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="3.0" unique-identifier="uid"><metadata><d:title>封面</d:title><d:identifier id="uid">c</d:identifier></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/><item id="c" href="c.png" media-type="image/png" properties="cover-image"/></manifest><spine><itemref idref="a"/></spine></package>')
        z.writestr('OPS/a.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><body><p>正文</p></body></html>')
        z.writestr('OPS/c.png',png(120,180,40))
    (books/'b.txt').write_text('阅读测试。\n'*50,encoding='utf-8');(books/'b.cover.png').write_bytes(png(60,90,200))
    (books/'c.txt').write_text('无封面。\n',encoding='utf-8')

    traces=[];screens=[]
    def run(script,*extra,completed_cache=0):
        capture=root/f'frames-{len(traces)}';capture.mkdir()
        command=[sys.argv[1],'--library',str(books),'--state-dir',str(state),*extra]
        environment=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT="library,"+script,PN_SIM_TIMING='1',PN_SIM_FRAME_DIR=str(capture),PN_SIM_CLOCK='12:34')
        if completed_cache and os.name=='posix':
            # 等真实落盘清理回执，而非假定1500ms涵盖所有fsync；SDL把SIGINT转换为正常QUIT。
            # Await actual persisted-clean receipts rather than assuming all fsyncs fit 1500ms; SDL converts SIGINT to a normal QUIT.
            assert script.endswith('quit');environment['PN_SIM_LIBRARY_SCRIPT']='library,'+script[:-4]+'wait:60000';environment['SDL_NO_SIGNAL_HANDLERS']='0'
            process=subprocess.Popen(command,env=environment,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            stdout=[];stderr=[];complete=threading.Event()
            def collect(stream,lines,watch=False):
                cleaned=0
                for line in stream:
                    lines.append(line)
                    if watch and 'profile phase=cover.cache-clean ' in line:
                        cleaned+=1
                        if cleaned==completed_cache:complete.set()
                stream.close()
            readers=[threading.Thread(target=collect,args=(process.stdout,stdout)),threading.Thread(target=collect,args=(process.stderr,stderr,True))]
            for reader in readers:reader.start()
            observed=complete.wait(30)
            if process.poll() is None:process.send_signal(signal.SIGINT)
            try:process.wait(timeout=10)
            except subprocess.TimeoutExpired:process.kill();process.wait()
            for reader in readers:reader.join(timeout=5);assert not reader.is_alive()
            r=subprocess.CompletedProcess(command,process.returncode,''.join(stdout),''.join(stderr));assert observed,(r.stdout,r.stderr)
        else:r=subprocess.run(command,env=environment,capture_output=True,text=True,timeout=60)
        assert r.returncode==0,(r.stdout,r.stderr)
        assert 'used=0 live=0' in r.stdout,r.stdout
        traces.append(r.stderr);screens.append(sorted(capture.glob('frame-*.pgm'))[-1].read_bytes())
        return r.stdout

    # 默认不写缓存：空闲解码两张封面后一次重绘。/ No cache by default: two covers decode during idle, then one redraw.
    out=run('wait:1500,quit');assert out.count('covers ready=2 status=0')==1,out
    assert not any((root/'tf').iterdir())
    # 显式缓存：首次解码并写入三条结论；再次启动全部来自缓存，无空闲重绘。/ Explicit cache: first run decodes and stores three outcomes; restart serves all from cache with no idle redraw.
    out=run('wait:1500,quit','--cover-cache',str(cache),completed_cache=3);assert out.count('covers ready=2 status=0')==1,out
    results=list(map(int,re.findall(r'profile phase=cover.finish .*? status=(\d+)',traces[-1])))
    assert results.count(0)==2 and results.count(3)==1 and all(s in (0,3,8) for s in results),traces[-1]
    assert traces[-1].count('profile phase=cover.cache-sync ')==3 and traces[-1].count('profile phase=cover.cache-clean ')==3,traces[-1]
    assert len(list(cache.glob('*.pnc')))==3,list(cache.iterdir())
    out=run('wait:1000,quit','--cover-cache',str(cache));assert 'covers ready' not in out,out
    assert 'profile phase=cover.finish ' not in traces[-1] and 'profile phase=cover.cache-write ' not in traces[-1],traces[-1]
    assert screens[-1]==screens[-2],'cold extraction and restarted cache produce identical shelf pixels'
    # 输入会释放暂存；同页重试复用已验证像素，退出必须释放租约。/ Input releases staging; same-page retries reuse verified pixels and exit releases leases.
    interrupted=root/'tf'/'.readpico'/'interrupted'
    out=run('wait:110,toc-down,wait:40,toc-down,wait:40,toc-up,wait:1600,quit','--cover-cache',str(interrupted),completed_cache=3)
    assert len(list(interrupted.glob('*.pnc')))==3 and not list(interrupted.glob('*.tmp')) and not list(interrupted.glob('*.bak')),out
    results=list(map(int,re.findall(r'profile phase=cover.finish .*? status=(\d+)',traces[-1])))
    # 输入可能中止尚未完成的同步解析；每本已验证结果只形成一次。/ Input may cancel unfinished synchronous parsing; each validated result is produced only once.
    assert results.count(0)==2 and results.count(3)==1 and all(s in (0,3,8) for s in results),traces[-1]
    print(f'cover window interrupted extraction results={results}; cancelled source attempts retained')
    # 选项只用于书库窗口。/ The option is valid for the library window only.
    assert subprocess.run([sys.argv[1],'--cover-cache',str(cache)],env=dict(os.environ,SDL_VIDEODRIVER='dummy'),capture_output=True).returncode==2
print('cover window: idle queue, single redraw, explicit cache store and restart hit passed')
