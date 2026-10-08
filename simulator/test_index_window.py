"""SDL书架拼音排序与字母跳转。 / SDL shelf pinyin ordering and letter jumps."""
import os,pathlib,subprocess,sys,tempfile
with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder);books=root/'books';books.mkdir()
    for name in ('安徒生童话.txt','Apple.txt','北京.txt','长城.txt','大海.txt','飞鸟集.txt','红楼梦.txt','金庸.txt','Zebra.txt','中国.txt'):(books/name).write_text('测试。\n',encoding='utf-8')
    def run(script):
        r=subprocess.run([sys.argv[1],'--library',str(books),'--state-dir',str(root/'state')],env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT=script),capture_output=True,text=True,timeout=60)
        assert r.returncode==0,(r.stdout,r.stderr)
        assert 'used=0 live=0' in r.stdout,r.stdout
        return r.stdout
    out=run('quit');assert 'library_page first=Apple.txt count=6' in out,out
    out=run('index,tap:259:980,quit');assert 'index open status=0' in out and 'index letter=z status=0 first=Zebra.txt' in out,out
    out=run('index,tap:259:230,quit');assert 'index letter=b status=0 first=北京.txt' in out,out
    out=run('index,back,quit');assert 'index letter=< status=0 first=Apple.txt' in out,out
print('index window: pinyin first page, z/b jumps and back passed')
