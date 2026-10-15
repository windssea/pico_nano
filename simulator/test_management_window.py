"""管理三键与面板返回，不跳过删除确认。 / Management keys and sheet return without skipping deletion confirmation."""
import os,pathlib,subprocess,sys,tempfile
with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder);books=root/'books';books.mkdir();state=root/'state';state.mkdir();fonts=root/'fonts';fonts.mkdir()
    book=books/'a.txt';book.write_text('管理键验证。\n'*50,encoding='utf-8')
    def run(script):
        print('management scenario:',script,flush=True)
        result=subprocess.run([sys.argv[1],'--library',str(books),'--state-dir',str(state),'--font-dir',str(fonts)],env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT='library,'+script),capture_output=True,text=True,timeout=60)
        assert result.returncode==0,(result.stdout,result.stderr)
        assert 'used=0 live=0' in result.stdout,result.stdout
        return result.stdout
    out=run('settings,tap:100:700,key-left,key-home,quit')
    assert 'settings command=1 status=0' in out and 'management home status=0' in out,out
    out=run('settings,key-confirm,key-left,key-home,quit')
    assert 'settings closed request=3' in out and 'font_manage command=1 status=0' in out,out
    out=run('rtap:100:520,tap:300:1060,key-left,key-home,quit')
    assert 'actions command=3 status=0' in out and 'actions command=4 status=0 active=1' in out,out
    assert book.exists()
    out=run('enter,styles,style-plus,key-home,next,quit,quit')
    assert 'style_ui command=2 status=0 active=0' in out and 'management home status=0' not in out,out
    out=run('enter,key-middle-long,key-home,key-home,quit')
    assert 'reading middle long settings status=0' in out and 'reading settings middle status=0 active=0' in out and 'reading middle home status=0' in out,out
    # 右键仅在已打开确认页才删除，读取记录保留。/ Right deletes only from the explicit confirmation page; reading records remain.
    out=run('rtap:100:520,tap:300:1060,key-confirm,quit')
    assert 'actions command=5 status=0' in out and not book.exists(),out
print('management keys: nested Back, Confirm, Home, reader cleanup and explicit delete passed')
