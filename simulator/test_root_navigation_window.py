"""固定导航首次按下、跨页接触和扫描中释放。 / Fixed-navigation first press, cross-page contact and releases during scans."""
import os,pathlib,shutil,subprocess,sys,tempfile
fixture=pathlib.Path(__file__).resolve().parents[1]/'tests/fixtures/reader_sample.txt'
with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder);books=root/'books';books.mkdir();shutil.copyfile(fixture,books/'reader.txt')
    def run(script,name,feedback=0,delay=0):
        frames=root/name;frames.mkdir();state=root/(name+'-state');state.mkdir()
        # 首帧早于窗口时钟初始化；先正常重画首页再比较完整像素。/ The first frame precedes window-clock initialization; repaint Home normally before comparing complete pixels.
        env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT='home,'+script,PN_SIM_FEEDBACK=str(feedback),PN_SIM_FRAME_DIR=str(frames),PN_SIM_PRESENT_DELAY_MS=str(delay),PN_SIM_CLOCK='12:34')
        r=subprocess.run([sys.argv[1],'--library',str(books),'--state-dir',str(state)],env=env,capture_output=True,text=True,timeout=90)
        assert r.returncode==0 and 'used=0 live=0' in r.stdout,(script,r.stdout,r.stderr)
        return r.stdout,sorted(frames.glob('frame-*.pgm'))[1:]
    for feedback in (0,1):
        # 脚本没有松手仍已显示设置，直接证明确认时机提前。/ Settings is displayed without any release in the script, directly proving earlier confirmation.
        out,frames=run('press:600:1150,wait:80,close-window','held'+str(feedback),feedback)
        assert out.count('navigation press hit=')==1 and 'settings open status=0' in out,out
        assert len(frames)==2 and frames[0].read_bytes()!=frames[1].read_bytes(),out
        out,frames=run('press:600:1150,move:80:1150,release:80:1150,wait:30,close-window','move'+str(feedback),feedback)
        assert out.count('navigation press hit=')==1 and 'settings command=' not in out and len(frames)==2,out
        out,frames=run('tap:600:1150,tap:250:1150,tap:450:1150,tap:80:1150,close-window','roots'+str(feedback),feedback)
        assert out.count('navigation press hit=')==4 and 'hub open status=0' in out and frames[-1].read_bytes()==frames[0].read_bytes(),out
        out,_=run('settings,press:100:700,move:80:1150,release:80:1150,close-window','slide'+str(feedback),feedback)
        assert 'navigation press hit=' not in out and 'settings command=' not in out,out
        out,_=run('settings,release:80:1150,close-window','orphan'+str(feedback),feedback)
        assert 'navigation press hit=' not in out and 'settings command=' not in out,out
        out,_=run('settings,tap:100:700,press:80:1150,release:80:1150,back,close-window','subpage'+str(feedback),feedback)
        assert 'navigation press hit=' not in out and 'settings command=6 status=0' in out,out
        out,_=run('press:600:1150,focus-lost,release:80:1150,tap:250:1150,close-window','focus'+str(feedback),feedback)
        assert out.count('navigation press hit=')==2,out
    out,_=run('race-nav:10:600:1150,tap:250:1150,close-window','racing',delay=180)
    assert 'navigation stale release consumed' in out and out.count('navigation press hit=')==2,out
print('root navigation: press before release, ownership, slide-in, orphan/subpage rejection, both feedback modes and stale scan release passed')
