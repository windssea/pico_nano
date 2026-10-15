"""两个真实SDL进程间复用语义文件，核对完整阅读帧；非面板计时。 / Semantic-file reuse between real SDL processes with full reading-frame checks; not panel timing."""
import os,pathlib,re,subprocess,sys,tempfile
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tests'/'host'))
from test_semantic_disk import create
with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder);books=root/'books';books.mkdir();create(books/'a.epub');state=root/'state'
    def run(name,script):
        capture=root/name;capture.mkdir();env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT='library,'+script,PN_SIM_FRAME_DIR=str(capture),PN_SIM_CLOCK='12:34',PN_SIM_TIMING='1')
        result=subprocess.run([sys.argv[1],'--library',str(books),'--font',sys.argv[2],'--state-dir',str(state)],env=env,capture_output=True,text=True,timeout=90)
        assert result.returncode==0 and 'used=0 live=0' in result.stdout,(result.stdout,result.stderr)
        frames=sorted(capture.glob('frame-*.pgm'));return result,frames
    first,frames=run('first','enter,wait:2200,back,quit');assert 'semantic cache stored' in first.stdout,first.stdout
    packet=next((state/'semantic').glob('*.pnsm'));assert 152<packet.stat().st_size<=384*1024
    # 最后一张是返回书架；前一张是刚读的正文。/ The final frame returns to the shelf; the preceding frame is the read body.
    reading=frames[-2].read_bytes()
    second,frames=run('second','enter,quit,quit');assert frames[-2].read_bytes()==reading,(second.stdout,second.stderr)
    assert re.search(r'profile phase=epub.model-load .*status=0',second.stderr) and 'phase=epub.model-build' not in second.stderr and 'phase=content.stream-sha' in second.stderr,second.stderr
    data=bytearray(packet.read_bytes());data[100]^=1;packet.write_bytes(data)
    third,frames=run('corrupt','enter,quit,quit');assert frames[-2].read_bytes()==reading and 'phase=epub.model-build' in third.stderr,(third.stdout,third.stderr)
    fourth,frames=run('menu','enter,tools,wait:2200,quit,quit,quit');assert 'semantic cache stored' not in fourth.stdout,fourth.stdout
print('semantic cache SDL: separate-process disk reuse, complete reading pixels, full SHA retained, no body XML build on hit, corruption rebuild and menu suppression passed')
