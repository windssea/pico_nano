"""实际窗口的三行书架、滑动、长按及已移除控件。 / Real-window three-row shelf, swipes, holds and removed controls."""
import os,pathlib,subprocess,sys,tempfile
with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder);books=root/'books';books.mkdir()
    # 仅公共输入边界旅程；真实书籍性能由私人样本矩阵另验。/ Public input-boundary journey only; private sample matrices separately verify actual books.
    for i in range(13):(books/f'b{i:02}.txt').write_text('Reader input fixture.\n'*100,encoding='utf8')
    def run(script):
        r=subprocess.run([sys.argv[1],'--library',str(books),'--state-dir',str(root/'state')],env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT='library,'+script+',close-window',PN_SIM_CLOCK='12:34'),capture_output=True,text=True,timeout=60)
        assert r.returncode==0 and 'used=0 live=0' in r.stdout,(script,r.stdout,r.stderr)
        return r.stdout
    out=run('press:550:500,move:150:500,release:150:500,press:150:500,move:550:500,release:550:500')
    assert 'library_page first=b00.txt count=9' in out and 'library_page first=b09.txt count=4' in out and 'shelf swipe command=41 index=9 status=0' in out and 'shelf swipe command=40 index=0 status=0' in out and 'library_open' not in out,out
    out=run('press:550:500,release:150:610,press:150:500,release:550:610')
    assert 'shelf swipe command=41 index=9 status=0' in out and 'shelf swipe command=40 index=0 status=0' in out and 'library_open' not in out,out
    out=run('press:100:400,wait:800,release:100:400')
    assert out.count('actions open index=0 status=0')==1 and 'shelf long press book=0' in out and 'library_open' not in out,out
    for script in ('tap:560:110,tap:100:1080,tap:600:1080','press:100:400,move:130:400,release:130:400','press:200:500,move:200:800,release:200:800','press:150:500,move:550:500,release:550:500'):
        out=run(script);assert 'library_open' not in out and 'actions open' not in out and 'shelf swipe command=' not in out,out
    out=run('tap:570:850');assert 'library_open name=b08.txt' in out and 'actions open' not in out,out
    out=run('tap:175:290');assert 'library_open name=b00.txt' in out and 'actions open' not in out,out
print('shelf gesture window: nine entries, both swipes, edge, long hold once, drag cancellation, removed search/footer actions and unobstructed book tap passed')
