"""SDL书架搜索：屏幕键盘输入拼音首字母，列出匹配，再点“清除”恢复。 / SDL shelf search: type pinyin initials on the on-screen keyboard, list matches, then clear to restore."""
import os,pathlib,subprocess,sys,tempfile
with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder);books=root/'books';books.mkdir()
    for name in ('北京.txt','慢读时光.txt','Apple.txt','Banana.txt'):(books/name).write_text('测试。\n',encoding='utf-8')
    def run(script):
        r=subprocess.run([sys.argv[1],'--library',str(books),'--state-dir',str(root/'state')],env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT=script),capture_output=True,text=True,timeout=60)
        assert r.returncode==0,(r.stdout,r.stderr)
        assert 'used=0 live=0' in r.stdout,r.stdout
        return r.stdout
    SEARCH='tap:560:110';B='tap:180:375';J='tap:386:467';DONE='tap:550:950';BACK='tap:60:60';DEL='tap:100:950'
    out=run('quit');assert 'library_page first=Apple.txt count=4' in out,out
    # 汉字书名按拼音首字母匹配：bj → 北京。/ Hanzi titles match by pinyin initials: bj finds 北京.
    out=run(','.join((SEARCH,B,J,DONE,'quit')));assert 'search query=bj status=0 count=1' in out and 'library_page first=北京.txt count=1' in out,out
    # 英文按子串匹配：单独的b命中Banana、北京。/ English matches as substrings: a lone b finds Banana and 北京.
    out=run(','.join((SEARCH,B,DONE,'quit')));assert 'search query=b status=0 count=2' in out,out
    # 退格删掉最后一个字符；返回不改搜索词；再点搜索入口（现为“清除”）恢复全部。/ Backspace drops the last character; back leaves the query alone; the entry (now "clear") restores everything.
    out=run(','.join((SEARCH,B,J,DEL,DONE,'quit')));assert 'search query=b status=0 count=2' in out,out
    out=run(','.join((SEARCH,B,BACK,'quit')));assert 'search query' not in out,out
    out=run(','.join((SEARCH,B,J,DONE,SEARCH,'quit')));assert 'library_page first=Apple.txt count=4' in out.split('search query=bj')[1],out
print('search window: pinyin-initial and English matching, backspace, back and clear passed')
