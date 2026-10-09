"""SDL进度跳转面板：打开、步进只改草稿、确认后可返回跳转前位置、取消不移动。 / SDL progress jump panel: open, stepping edits the draft, a confirmed jump can be undone, cancel does not move."""
import os,pathlib,subprocess,sys,tempfile
with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder);books=root/'books';books.mkdir();state=root/'state'
    (books/'a.txt').write_text(''.join(f'第{i}段：文字沿着清晰的行列展开，调整字距和段落间距，找到舒服的阅读节奏。\n' for i in range(600)),encoding='utf-8')
    def run(script):
        r=subprocess.run([sys.argv[1],'--library',str(books),'--state-dir',str(state)],env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT=script),capture_output=True,text=True,timeout=120)
        assert r.returncode==0,(r.stdout,r.stderr)
        assert 'used=0 live=0' in r.stdout,r.stdout
        return r.stdout
    # 点页脚右侧打开面板；+10两次再确认；返回跳转前位置。/ Tap the footer's right side to open; +10 twice, confirm, then return to the pre-jump position.
    out=run('enter,tap:540:1180,tap:540:690,tap:540:690,tap:300:860,mark-return,back,quit,quit')
    for line in ('jump_ui open status=0','jump_ui command=13 status=0 active=1 draft=10','jump_ui command=13 status=0 active=1 draft=20','jump_ui command=2 status=0 active=0','bookmark_return status=0'):
        assert line in out,(line,out)
    # 取消：不移动，面板关闭。/ Cancel: no move and the panel closes.
    out=run('enter,jump,tap:540:690,back,mark-return,back,quit')
    assert 'jump_ui command=1 status=0 active=0' in out and 'bookmark_return status=0' not in out,out
print('jump window: footer entry, draft stepping, confirm with return, and cancel passed')
