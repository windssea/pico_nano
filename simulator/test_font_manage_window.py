"""SDL字体管理：设全局默认、删除确认、删除后仍能打开书。 / SDL font management: set default, confirmed delete, books still open afterwards."""
import os,pathlib,shutil,subprocess,sys,tempfile
with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder);books=root/'books';books.mkdir();(books/'a.txt').write_text('字体管理窗口测试。\n'*40,encoding='utf-8')
    fonts=root/'fonts';fonts.mkdir();state=root/'state';state.mkdir()
    source=pathlib.Path(__file__).resolve().parents[1]/'assets'/'fonts'/'read-pico-ui.ttf'
    shutil.copy(source,fonts/'a-ui.ttf');shutil.copy(source,fonts/'b-copy.ttf')
    def run(script,*extra):
        r=subprocess.run([sys.argv[1],'--library',str(books),'--state-dir',str(state),*extra],env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT=script),capture_output=True,text=True,timeout=120)
        assert r.returncode==0,(r.stdout,r.stderr)
        assert 'used=0 live=0' in r.stdout,r.stdout
        return r.stdout
    out=run('fonts,tap:100:240,tap:100:1000,tap:500:1000,back,tap:500:1000,tap:500:1150,back,quit','--font-dir',str(fonts))
    for line in ('font_manage open status=0','font_manage command=16 status=0 screen=1 active=1','font_manage command=4 status=0 screen=1 active=1',
                 'font_manage command=5 status=0 screen=2 active=1','font_manage command=7 status=0 screen=1 active=1','font_manage command=6 status=0 screen=0 active=1',
                 'font_manage command=1 status=0 screen=0 active=0'):
        assert line in out,(line,out)
    assert not (fonts/'a-ui.ttf').exists() and (fonts/'b-copy.ttf').exists()
    assert (state/'fonts.a').exists() or (state/'fonts.b').exists()
    # 全局字体已删除：书仍可打开，记录不变。/ The global font is deleted: the book still opens and the record is unchanged.
    before={p.name:p.read_bytes() for p in state.glob('fonts.*')}
    out=run('enter,back,quit');assert 'library_open name=a.txt' in out,out
    assert before=={p.name:p.read_bytes() for p in state.glob('fonts.*')}
    assert subprocess.run([sys.argv[1],'--font-dir',str(fonts)],env=dict(os.environ,SDL_VIDEODRIVER='dummy'),capture_output=True).returncode==2
print('font manage window: open, detail, global default, delete cancel/confirm, reading after deletion and option validation passed')
