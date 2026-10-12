"""SDL书籍操作：右键（长按）打开面板、收藏与收藏页、删除文件需确认。 / SDL book actions: right click (long press) opens the sheet, favorites and the favorites page, deleting needs confirmation."""
import os,pathlib,subprocess,sys,tempfile
with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder);books=root/'books';books.mkdir();state=root/'state';state.mkdir()
    for name in ('a.txt','b.txt'):(books/name).write_text('测试。\n',encoding='utf-8')
    frames=[]
    def run(script):
        capture=root/'frames';capture.mkdir(exist_ok=True)
        for f in capture.glob('*.pgm'):f.unlink()
        r=subprocess.run([sys.argv[1],'--library',str(books),'--state-dir',str(state)],env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT="library,"+script,PN_SIM_FRAME_DIR=str(capture)),capture_output=True,text=True,timeout=60)
        assert r.returncode==0,(r.stdout,r.stderr)
        assert 'used=0 live=0' in r.stdout,r.stdout
        files=sorted(capture.glob("*.pgm"));assert files
        frames.append(files[-1].read_bytes().split(b"\n",3)[3])
        return r.stdout
    COVER='rtap:100:520';FAVORITE='tap:300:960';DELETE='tap:300:1060';CONFIRM='tap:500:940';CANCEL='tap:150:940';CLOSE='tap:300:300';FAVORITES='tap:300:190'
    run('quit');unmarked=frames[-1]
    out=run(','.join((COVER,FAVORITE,CLOSE,FAVORITES,'quit')))
    for line in ('actions open index=0 status=0 favorite=0','actions command=2 status=0 active=1 favorite=1','actions command=4 status=0 active=0','favorites_page start=0 count=1'):assert line in out,(line,out)
    # 收藏与取消的封面像素确实改变，重启后仍然正确。/ Cover pixels change on toggle and stay correct after restarting.
    run('quit');marked=frames[-1]
    def badge(image):return b''.join(image[y*684+42:y*684+70] for y in range(294,322))
    assert badge(unmarked)!=badge(marked)
    run('tap:600:190,quit');marked_list=frames[-1]
    # 再次打开：已收藏；取消收藏。/ Reopen: already a favorite; remove it.
    out=run(','.join((COVER,FAVORITE,CLOSE,'quit')));assert 'actions open index=0 status=0 favorite=1' in out and 'actions command=2 status=0 active=1 favorite=0' in out,out
    assert badge(frames[-1])==badge(unmarked)
    run('tap:600:190,quit');unmarked_list=frames[-1]
    def list_badge(image):return b''.join(image[y*684+76:y*684+106] for y in range(279,309))
    assert list_badge(marked_list)!=list_badge(unmarked_list)
    # 删除：先进确认页，取消不删；确认才删。/ Delete: the confirmation page first, cancel keeps the file, confirm deletes it.
    out=run(','.join((COVER,DELETE,CANCEL,CLOSE,'quit')));assert (books/'a.txt').exists() and 'actions command=3 status=0 active=1' in out,out
    out=run(','.join((COVER,DELETE,CONFIRM,'quit')));assert not (books/'a.txt').exists() and (books/'b.txt').exists() and 'actions command=5 status=0 active=0' in out,out
    # 封面角落“⋯”同样打开面板。/ The "⋯" in the cover corner opens the sheet too.
    out=run('tap:190:300,tap:300:300,quit');assert 'actions open index=0 status=0' in out,out
print('actions window: long press sheet, favorites toggle and page, confirmed delete passed')
