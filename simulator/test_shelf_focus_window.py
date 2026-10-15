"""真实书架键盘焦点局部更新、往返像素与选书。 / Real shelf keyboard focus patches, round-trip pixels and book selection."""
import os,pathlib,re,subprocess,sys,tempfile
with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder);books=root/'books';books.mkdir()
    for i in range(9):(books/f'Book{i}.txt').write_text('Original reading fixture.\n'*80,encoding='utf8')
    for mode in ('grid','list'):
        capture=root/mode;capture.mkdir()
        script='library,'+('tap:620:180,' if mode=='list' else '')+'wait:800,toc-down,toc-down,toc-up,wait:100,quit'
        env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT=script,PN_SIM_FRAME_DIR=str(capture),PN_SIM_CLOCK='12:34',PN_SIM_FEEDBACK='1')
        result=subprocess.run([sys.argv[1],'--library',str(books),'--font',sys.argv[2],'--state-dir',str(root/(mode+'-state'))],env=env,capture_output=True,text=True,timeout=60)
        assert result.returncode==0 and 'used=0 live=0' in result.stdout,(result.stdout,result.stderr)
        moves=re.findall(r'shelf_focus before=(-?\d+) after=(\d+) partial=1 status=0',result.stdout);assert moves==[('-1','0'),('0','1'),('1','0')],result.stdout
        frames=sorted(capture.glob('frame-*.pgm'));assert len(frames)>=5
        pixels=[p.read_bytes().split(b'\n',3)[3] for p in frames[-3:]];assert pixels[0]==pixels[2] and pixels[0]!=pixels[1]
        if mode=='grid':
            for p in pixels[1:]:assert p[:266*684]==pixels[0][:266*684] and p[528*684:]==pixels[0][528*684:]
        else:
            for p in pixels[1:]:
                for y in range(1216):assert p[y*684:y*684+18]==pixels[0][y*684:y*684+18] and p[y*684+28:(y+1)*684]==pixels[0][y*684+28:(y+1)*684]
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT='library,wait:800,toc-down,toc-down,enter,next,back,quit')
    selected=subprocess.run([sys.argv[1],'--library',str(books),'--state-dir',str(root/'selected')],env=env,capture_output=True,text=True,timeout=60)
    assert selected.returncode==0 and 'library_open name=Book1.txt' in selected.stdout and 'library_return status=0' in selected.stdout and 'used=0 live=0' in selected.stdout,(selected.stdout,selected.stderr)
print('shelf focus window: real keys with external fallback, grid/list partial pixels, exact focus round trip, unchanged headings/navigation, selected-book reading and return passed')
