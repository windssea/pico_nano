"""反馈空闲定稿只重呈现当前图，不吞输入，不重画字形；失败限频保留。 / Idle settling re-presents current pixels without swallowing input/repainting glyphs, retaining rate-limited failures."""
import os,pathlib,subprocess,sys,tempfile
with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder);books=root/'books';books.mkdir();(books/'a.txt').write_text('原创空闲定稿测试。\n'*100,encoding='utf8')
    state=root/'state';state.mkdir()
    def run(script,name,fail=False):
        capture=root/name;capture.mkdir()
        env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT='library,settings,'+script,PN_SIM_FEEDBACK='1',PN_SIM_CLOCK='12:34',PN_SIM_FRAME_DIR=str(capture),PN_SIM_PRESENT_DELAY_MS='25')
        if fail:env['PN_SIM_SETTLE_FAIL_ONCE']='1'
        r=subprocess.run([sys.argv[1],'--library',str(books),'--state-dir',str(state)],env=env,capture_output=True,text=True,timeout=120)
        assert r.returncode==0 and 'used=0 live=0' in r.stdout,(r.stdout,r.stderr)
        return r.stdout,capture
    cancel='press:100:600,move:100:850,release:100:850'
    out,cap=run(cancel+',wait:750,quit,quit','quiet')
    settles=[line for line in out.splitlines() if line.startswith('feedback ') and 'settle=1' in line]
    assert len(settles)==1 and 'box=0,512,684,192' in settles[0],out
    restore=next(cap.glob('*restore.pgm'));settle=next(cap.glob('*settle.pgm'));assert restore.read_bytes()==settle.read_bytes()
    lines=[line for line in out.splitlines() if line.startswith('feedback ')];assert len({line.split('revision=')[1].split()[0] for line in lines})==1,out
    out,cap=run(cancel+',wait:150,press:100:700,move:100:850,release:100:850,wait:750,quit,quit','merge')
    settles=[line for line in out.splitlines() if line.startswith('feedback ') and 'settle=1' in line]
    assert len(settles)==1 and 'box=0,512,684,256' in settles[0],out
    out,cap=run('press:100:600,wait:650,release:100:600,wait:750,back,back,quit','held')
    assert 'settle=1' not in out and out.count('settings command=5 status=0')==1,out
    out,cap=run('tap:100:700,tap:340:240,wait:750,back,back,quit','noop')
    assert 'settle=1' in out and 'settings command=12 status=0' in out,out
    out,cap=run(cancel+',wait:1900,quit,quit','retry',True)
    assert out.count('feedback_settle status=')==1 and out.count('settle=1')==1,out
    restore=next(cap.glob('*restore.pgm'));settle=next(cap.glob('*settle.pgm'));assert restore.read_bytes()==settle.read_bytes()
print('feedback idle window: quiet one-scan merge, current pixels, stable revision, held/content supersession, no-op settling and failure retry passed')
