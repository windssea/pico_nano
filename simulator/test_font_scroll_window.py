"""真实字体列表跨页拖动、逐相回执和灰阶定稿，非面板验收。 / Real font list cross-page drag, phase receipts and gray settling, not panel acceptance."""
import os,pathlib,re,subprocess,sys,tempfile
with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder);books=root/'books';books.mkdir();(books/'a.txt').write_text('Original fixture.\n'*100,encoding='utf8');fonts=root/'fonts';fonts.mkdir()
    source=pathlib.Path(sys.argv[2]).read_bytes()
    for i in range(24):(fonts/f'Font{i:02}.ttf').write_bytes(source)
    capture=root/'capture';capture.mkdir()
    script='library,fonts,press:400:820,move:400:520,wait:180,move:400:350,wait:120,release:400:350,wait:1800,quit,quit'
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT=script,PN_SIM_FOLLOW='1',PN_SIM_FEEDBACK='1',PN_SIM_FOLLOW_TRACE='1',PN_SIM_FOLLOW_CAPTURE='1',PN_SIM_FRAME_DIR=str(capture),PN_SIM_CLOCK='12:34')
    result=subprocess.run([sys.argv[1],'--library',str(books),'--font-dir',str(fonts),'--state-dir',str(root/'state')],env=env,capture_output=True,text=True,timeout=90)
    assert result.returncode==0 and 'used=0 live=0' in result.stdout,(result.stdout,result.stderr)
    assert 'font_scroll enabled=1' in result.stdout and 'font_motion stage=1 status=0' in result.stdout and 'font_motion stage=3 status=0' in result.stdout,result.stdout
    positions=[int(v) for v in re.findall(r'font_motion stage=1 status=0 offset=(\d+)',result.stdout)];assert positions and max(positions)>=600,result.stdout
    assert 'screen=1' not in result.stdout,result.stdout
    assert 'feedback pressed=1' in result.stdout,result.stdout
    revisions=re.findall(r'font_motion.*revision=(\d+)',result.stdout);assert len(set(revisions))==1,result.stdout
    def pgm(path):return path.read_bytes().split(b'\n',3)[3]
    normalized=pgm(next(capture.glob('follow-*-0.pgm')))
    phases=list(capture.glob('follow-*-1.pgm'));assert phases
    for path in phases:
        pixels=pgm(path)
        for y in range(1216):
            for x0,x1 in ((0,32),(652,684)) if 334<=y<934 else ((0,684),):assert pixels[y*684+x0:y*684+x1]==normalized[y*684+x0:y*684+x1]
    settled=pgm(next(capture.glob('follow-*-3.pgm')));assert any(v not in (0,255) for y in range(334,934) for v in settled[y*684+32:y*684+652])
    # 故障恢复、失焦与导航采用真实窗口/队列路径，不改变字体内容。 / Fault recovery, focus loss and navigation use the real window/queue path, retaining font contents.
    journeys={
        'failed':('library,fonts,press:400:820,move:400:520,move:400:350,release:400:350,wait:1800,quit,quit',{'PN_SIM_FOLLOW_FAIL_ONCE':'1'}),
        'focus':('library,fonts,press:400:820,move:400:520,move:400:350,focus-lost,release:400:350,wait:900,quit,quit',{}),
        'navigate':('library,fonts,press:400:820,move:400:520,move:400:350,key-home,release:400:350,wait:900,quit',{}),
        'low':('library,fonts,press:400:820,move:400:520,move:400:350,release:400:350,wait:900,quit,quit',{}),
    }
    for name,(journey,extra) in journeys.items():
        case_env=dict(env,PN_SIM_LIBRARY_SCRIPT=journey,PN_SIM_FOLLOW_CAPTURE='0',**extra)
        command=[sys.argv[1],'--library',str(books),'--font-dir',str(fonts),'--state-dir',str(root/name)]
        if name=='low':command+=['--budget',str(2*1024*1024)]
        case=subprocess.run(command,env=case_env,capture_output=True,text=True,timeout=90)
        assert case.returncode==0 and 'used=0 live=0' in case.stdout,(name,case.stdout,case.stderr)
        assert 'screen=1' not in case.stdout,(name,case.stdout)
        if name=='failed':assert 'font_motion stage=1 status=0' not in case.stdout and 'font_motion stage=4 status=0' in case.stdout,case.stdout
        elif name=='focus':assert 'font_motion stage=3 status=0' in case.stdout,case.stdout
        elif name=='navigate':assert 'font_motion stage=3' not in case.stdout,case.stdout
        else:assert 'font_motion' not in case.stdout,case.stdout
print('font scroll window: valid fonts, cross-page inertia, phase receipts, stable view, fixed outer pixels, gray/native tiles, failed scan recovery, focus/navigation exits and low budget passed')
