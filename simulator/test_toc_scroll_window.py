"""真实目录窗口连续浏览、固定框外和恢复；不作为面板测量。 / Real TOC window motion, fixed exterior and recovery; not panel measurement."""
import os,pathlib,re,subprocess,sys,tempfile
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tests'/'host'))
from toc_motion_fixture import create

with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder);books=root/'books';books.mkdir();create(books/'a.epub')
    capture=root/'capture';capture.mkdir()
    def run(name,script,extra=None,low=False):
        env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT=script,PN_SIM_FOLLOW='1',PN_SIM_FEEDBACK='1',PN_SIM_FOLLOW_TRACE='1',PN_SIM_FOLLOW_CAPTURE='1' if name=='scroll' else '0',PN_SIM_FRAME_DIR=str(capture),PN_SIM_CLOCK='12:34')
        env.update(extra or {})
        command=[sys.argv[1],'--library',str(books),'--font',sys.argv[2],'--state-dir',str(root/name)]
        if low:command+=['--budget',str(2*1024*1024)]
        result=subprocess.run(command,env=env,capture_output=True,text=True,timeout=90)
        assert result.returncode==0 and 'used=0 live=0' in result.stdout,(name,result.stdout,result.stderr)
        return result.stdout
    drag='library,enter,toc,press:400:900,move:400:500,wait:180,move:400:250,wait:120,release:400:250,wait:1800'
    out=run('scroll',drag+',back,back,continue,back,quit')
    assert 'toc_scroll enabled=1 status=0' in out and 'toc_motion stage=1 status=0' in out and 'toc_motion stage=3 status=0' in out,out
    positions=[int(v) for v in re.findall(r'toc_motion stage=1 status=0 offset=(\d+)',out)];assert positions and max(positions)>=650,out
    revisions=re.findall(r'toc_motion.*revision=(\d+)',out);assert len(set(revisions))==1,out
    locations=re.findall(r'epub_library_position path=(\S+) element=(\d+) run=(\d+) offset=(\d+)',out);assert len(locations)==2 and locations[0]==locations[1],out
    assert 'feedback pressed=1' in out,out
    def pgm(path):return path.read_bytes().split(b'\n',3)[3]
    normalized=pgm(next(capture.glob('follow-*-0.pgm')))
    for path in capture.glob('follow-*-1.pgm'):
        pixels=pgm(path)
        for y in range(1216):
            for x0,x1 in ((0,32),(652,684)) if 148<=y<1028 else ((0,684),):assert pixels[y*684+x0:y*684+x1]==normalized[y*684+x0:y*684+x1],(path,y,x0)
    settled=pgm(next(capture.glob('follow-*-3.pgm')));assert any(v not in (0,255) for y in range(148,1028) for v in settled[y*684+32:y*684+652])
    failed=run('failed',drag+',back,back,quit',{'PN_SIM_FOLLOW_FAIL_ONCE':'1'})
    assert 'toc_motion stage=4 status=0' in failed,failed
    focus=run('focus','library,enter,toc,press:400:900,move:400:500,focus-lost,release:400:500,wait:900,back,back,quit')
    assert 'toc_motion stage=3 status=0' in focus,focus
    low=run('low',drag+',back,back,quit',low=True)
    assert 'toc_scroll enabled=0 status=0' in low and 'toc_motion' not in low,low
print('TOC scroll window: actual events, cross-page phases, fixed exterior, gray settlement, unchanged reopen position, feedback, recovery/focus and low-budget fallback passed')
