"""SDL设置页：翻页开关保存与重开、子页跳转。 / SDL settings page: page-turn switches saved across restarts and sub-page jumps."""
import os,pathlib,subprocess,sys,tempfile
with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder);books=root/'books';books.mkdir();(books/'a.txt').write_text('设置窗口测试。\n'*20,encoding='utf-8')
    state=root/'state';state.mkdir();fonts=root/'fonts';fonts.mkdir()
    def run(script,*extra):
        r=subprocess.run([sys.argv[1],'--library',str(books),'--state-dir',str(state),*extra],env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT=script),capture_output=True,text=True,timeout=120)
        assert r.returncode==0,(r.stdout,r.stderr)
        assert 'used=0 live=0' in r.stdout,r.stdout
        return r.stdout
    out=run('settings,tap:100:530,tap:100:640,back,quit')
    assert 'settings open status=0 flags=0' in out and 'settings command=8 status=0 flags=1 active=1' in out and 'settings command=9 status=0 flags=3 active=1' in out,out
    assert 'settings closed request=0' in out and (state/'input.a').exists() or (state/'input.b').exists()
    out=run('settings,back,quit');assert 'settings open status=0 flags=3' in out,out
    # 不能关掉全部翻页方式。/ All page-turn methods cannot be switched off.
    out=run('settings,tap:100:750,tap:100:820,back,quit');assert 'settings command=11 status=4 flags=7 active=1' in out,out
    # 子页：字体管理在给出--font-dir时打开。/ Sub-page: font management opens when --font-dir is given.
    out=run('settings,tap:100:220,back,quit','--font-dir',str(fonts));assert 'settings closed request=3' in out and 'font_manage command=1 status=0 screen=0 active=0' in out,out
print('settings window: switches persist across restarts, not-all-off refusal and sub-page jump passed')
