"""按下不选择，释放/滑出/窗口失焦恢复，真实端口回执保持输入身份。 / Press without selection, release/leave/focus restoration and real port receipts preserving input identity."""
import os,pathlib,subprocess,sys,tempfile
with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder);books=root/'books';books.mkdir();(books/'a.txt').write_text('原创反馈测试。\n'*150,encoding='utf-8')
    state=root/'state';state.mkdir()
    def run(script,name):
        capture=root/name;capture.mkdir()
        env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT='library,settings,'+script,PN_SIM_FEEDBACK='1',PN_SIM_FRAME_DIR=str(capture),PN_SIM_PRESENT_DELAY_MS='25',PN_SIM_CLOCK='12:34')
        r=subprocess.run([sys.argv[1],'--library',str(books),'--state-dir',str(state)],env=env,capture_output=True,text=True,timeout=120)
        assert r.returncode==0 and 'used=0 live=0' in r.stdout,(r.stdout,r.stderr)
        return r.stdout,capture
    out,cap=run('press:100:600,wait:80,move:100:850,release:100:850,quit,quit','leave')
    assert 'feedback pressed=1 hit=5' in out and 'feedback pressed=0 hit=5' in out,out
    assert 'settings command=5 ' not in out and 'settings command=7 ' not in out,out
    normal=sorted(cap.glob('frame-*.pgm'));feedback=sorted(cap.glob('feedback-*.pgm'))
    assert len(feedback)==2
    # 选按下之前最后完成的真实页面，启动首页/书架帧不硬编码序号。/ Use the last real page completed before press, without hardcoding boot/home/shelf frame indices.
    before=max((p for p in normal if p.stat().st_mtime_ns<=feedback[0].stat().st_mtime_ns),key=lambda p:(p.stat().st_mtime_ns,p.name))
    assert before.read_bytes()==feedback[1].read_bytes()
    original=before.read_bytes().split(b'\n',3)[3];pressed=feedback[0].read_bytes().split(b'\n',3)[3]
    assert original!=pressed
    for y in range(1216):
        for x in range(684):
            if not (32<=x<652 and 550<=y<650):assert original[y*684+x]==pressed[y*684+x]
    lines=[line for line in out.splitlines() if line.startswith('feedback ')]
    assert lines[0].split('revision=')[1].split()[0]==lines[1].split('revision=')[1].split()[0],out
    out,cap=run('press:100:600,wait:60,release:100:600,tap:100:680,tap:100:680,back,back,quit','commit')
    assert out.count('settings command=5 status=0')==1 and 'settings command=8 status=0 flags=193' in out and 'settings command=8 status=0 flags=192' in out,out
    assert 'feedback pressed=0 hit=5' not in out,out  # 新内容回执替代反馈 / New content supersedes feedback.
    out,cap=run('press:100:600,focus-lost,release:100:600,quit,quit','focus')
    assert 'settings command=5 ' not in out and 'feedback pressed=0 hit=5' in out,out
    out,cap=run('tap:100:700,tap:340:240,back,back,quit','same')
    assert 'settings command=12 status=0' in out and 'feedback pressed=0 hit=12' in out,out
    out,cap=run('tap:100:420,press:550:560,wait:60,release:550:560,back,back,quit','tile')
    assert 'settings command=22 status=0' in out and 'box=350,460,302,210' in out,out
print('feedback window: press-only, release once, leave/focus cancellation, exact restoration/outer pixels, no-op restoration and unchanged feedback revision passed')
