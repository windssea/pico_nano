"""真实窗口控制器的持久索引、续读和书签隔离。 / Persistent indexes, resume and bookmark isolation through the real window controller."""
import hashlib, os, pathlib, re, subprocess, sys, tempfile
with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder);books=root/'books';books.mkdir();state=root/'state';covers=root/'covers'
    (books/'A-long.txt').write_bytes((b'Page boundaries keep the original position.\n'*25000))
    for number in range(999): (books/f'Book{number:04d}.txt').write_text('hello',encoding='utf-8')
    def run(script):
        env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT=script,PN_SIM_TIMING='1')
        result=subprocess.run([sys.argv[1],'--library',str(books),'--state-dir',str(state),'--cover-cache',str(covers)],env=env,capture_output=True,text=True,timeout=90)
        assert result.returncode==0 and 'used=0 live=0' in result.stdout,(result.stdout,result.stderr)
        assert 'status=5' not in result.stdout,(result.stdout,result.stderr)
        return result
    first=run('library,wait:1200,enter,jump,'+'library-next,'*9+'enter,'+'next,'*36+'bookmarks,mark-add,back,home,quit')
    assert 'catalog index built' in first.stdout and 'library_return status=0' in first.stdout,first.stdout
    positions=re.findall(r'library_confirmed offset=(\d+)',first.stdout);assert positions and int(positions[-1])>900000,first.stdout
    saved=int(positions[-1]);assert list((state/'indexes').glob('*.pni')) and (covers/'library/library.pni').exists()
    marks={p.relative_to(state):hashlib.sha256(p.read_bytes()).hexdigest() for p in state.rglob('*') if p.is_file() and '.marks/' in p.as_posix()};assert marks
    resumed=run('continue,previous,home,quit');assert f'library_position offset={saved}' in resumed.stdout,resumed.stdout
    previous=re.findall(r'library_confirmed offset=(\d+)',resumed.stdout);assert previous and int(previous[-1])<saved
    assert marks=={p.relative_to(state):hashlib.sha256(p.read_bytes()).hexdigest() for p in state.rglob('*') if p.is_file() and '.marks/' in p.as_posix()}
    # 只删除可重建文件；继续阅读显示锚点不改变。/ Remove only rebuildable files; continue reading preserves the displayed anchor.
    for p in (state/'indexes').glob('*.pni'): p.unlink()
    (covers/'library/library.pni').unlink()
    rebuilt=run('continue,previous,home,library,wait:1200,library-next,library-previous,quit')
    assert f'library_position offset={previous[-1]}' in rebuilt.stdout and 'catalog index built' in rebuilt.stdout,rebuilt.stdout
    assert marks=={p.relative_to(state):hashlib.sha256(p.read_bytes()).hexdigest() for p in state.rglob('*') if p.is_file() and '.marks/' in p.as_posix()}
    print('Window index: 1000 books, deep resume reverse, cache deletion/rebuild and unchanged bookmarks passed')
