"""SDL列表模式：切换后每页5本，翻页、搜索与回到网格都按同样的页容量。 / SDL list mode: five per page after toggling, with paging, search and returning to the grid using the same capacity."""
import os,pathlib,subprocess,sys,tempfile
with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder);books=root/'books';books.mkdir()
    for name in 'abcdefgh':(books/f'{name}book.txt').write_text('测试。\n',encoding='utf-8')
    def run(script):
        r=subprocess.run([sys.argv[1],'--library',str(books),'--state-dir',str(root/'state')],env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT=script),capture_output=True,text=True,timeout=60)
        assert r.returncode==0,(r.stdout,r.stderr)
        assert 'used=0 live=0' in r.stdout,r.stdout
        return r.stdout
    LAYOUT='tap:600:360'
    out=run('quit');assert 'library_page first=abook.txt count=6' in out,out
    # 切到列表：第一页5本，下一页3本，再翻回上一页仍是5本。/ Toggle to list: five on the first page, three on the next, and back again five.
    out=run(','.join((LAYOUT,'library-next','library-previous','quit')))
    pages=[l for l in out.splitlines() if l.startswith('library_page')]
    assert pages==['library_page first=abook.txt count=6','library_page first=abook.txt count=5','library_page first=fbook.txt count=3','library_page first=abook.txt count=5'],pages
    # 再点一次回到网格每页6本。/ Toggling again returns to six per page.
    out=run(','.join((LAYOUT,LAYOUT,'quit')))
    assert [l for l in out.splitlines() if l.startswith('library_page')][-1]=='library_page first=abook.txt count=6',out
    # 搜索结果也按5本分页。/ Search results page by five as well.
    out=run(','.join((LAYOUT,'tap:560:110','tap:180:375','tap:550:950','quit')))
    assert 'search query=b status=0 count=5' in out,out
print('list window: toggle, five-per-page paging, search and grid restore passed')
