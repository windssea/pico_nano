"""SDL设置页：翻页开关保存与重开、子页跳转。 / SDL settings page: page-turn switches saved across restarts and sub-page jumps."""
import os,pathlib,subprocess,sys,tempfile
with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder);books=root/'books';books.mkdir();(books/'a.txt').write_text('设置窗口测试。\n'*200,encoding='utf-8')
    state=root/'state';state.mkdir();fonts=root/'fonts';fonts.mkdir()
    def run(script,*extra):
        r=subprocess.run([sys.argv[1],'--library',str(books),'--state-dir',str(state),*extra],env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT="library,"+script),capture_output=True,text=True,timeout=120)
        assert r.returncode==0,(r.stdout,r.stderr)
        assert 'used=0 live=0' in r.stdout,r.stdout
        return r.stdout
    # 翻页开关在“按键与手势”子页。/ The page-turn switches live on the keys-and-gestures page.
    out=run('settings,tap:100:600,tap:100:680,tap:100:780,back,back,quit')
    assert 'settings open status=0 flags=192' in out and 'settings command=8 status=0 flags=193 active=1' in out and 'settings command=9 status=0 flags=195 active=1' in out,out
    assert 'settings closed request=0' in out and (state/'input.a').exists() or (state/'input.b').exists()
    out=run('settings,back,quit');assert 'settings open status=0 flags=195' in out,out
    # 日期页在关于页内，PC联网按钮禁用，返回层级不跳回书架。 / The date page is inside About; PC network action is disabled and Back preserves hierarchy.
    out=run('settings,tap:100:950,tap:100:870,tap:100:740,back,back,back,quit')
    assert 'settings command=29 status=0' in out and 'settings command=30' not in out and 'settings command=31' not in out,out
    assert out.count('settings command=1 status=0')==3,out
    # WiFi网络：屏幕键盘输入名称ab与密码12345678；PC不能测试连接，选择仍然保存后写入状态目录。/ WiFi: type the name ab and password 12345678; the PC cannot test the connection, so saving anyway writes the state directory.
    out=run('settings,tap:100:850,tap:300:384,tap:78:948,tap:403:1044,tap:550:632,tap:46:756,tap:111:756,tap:176:756,tap:241:756,tap:306:756,tap:371:756,tap:436:756,tap:501:756,tap:550:632,tap:300:536,back,back,quit')
    assert 'settings command=37 status=0' in out and 'settings command=225 status=0' in out and 'settings command=226 status=0' in out and out.count('settings command=40 status=0')==2 and 'settings wifi_test status=0' in out and 'settings command=48 status=0' in out,out
    assert (state/'network.a').exists() or (state/'network.b').exists(),out
    # 不能关掉全部翻页方式。/ All page-turn methods cannot be switched off.
    out=run('settings,tap:100:600,tap:100:880,tap:100:980,back,back,quit');assert 'settings command=11 status=4 flags=199 active=1' in out,out
    # 子页：字体管理在给出--font-dir时打开。/ Sub-page: font management opens when --font-dir is given.
    # 字体管理返回设置页（不是书架），再返回才到书架。/ Back from font management returns to Settings, not the shelf; one more Back reaches the shelf.
    out=run('settings,tap:100:220,back,back,quit','--font-dir',str(fonts));assert 'settings closed request=3' in out and 'font_manage command=1 status=0 screen=0 active=0' in out and 'settings closed request=0' in out.split('font_manage command=1')[1],out
    # 全局刷新周期保存；原动画行不再命中，锁屏热恢复不重开书。/ Save global periods; the legacy animation row is inert and warm resume never reopens.
    out=run('settings,tap:100:700,tap:100:240,tap:340:940,back,back,enter,next,previous,lock,lock,quit,quit')
    assert 'settings command=13 status=0 flags=215' in out and 'settings command=27' not in out and 'lock_preview wake status=0' in out,out
    out=run('settings,tap:100:700,tap:560:240,tap:560:940,back,back,enter,next,previous,quit,quit')
    assert 'settings open status=0 flags=215' in out and 'settings command=14 status=0 flags=231' in out and 'settings command=28' not in out,out
print('settings window: switches and global refresh periods persist, legacy animation row inert, warm restore, not-all-off refusal and sub-page jump passed')
