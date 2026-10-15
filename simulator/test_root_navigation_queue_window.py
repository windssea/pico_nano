"""扫描中第二次底栏导航与串行旅程整帧相同，旧内容触点仍丢弃。 / A second root navigation during a scan matches serial frames, while old content contacts remain rejected."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

fixture=Path(__file__).resolve().parents[1]/'tests/fixtures/reader_sample.txt'
with tempfile.TemporaryDirectory() as directory:
    root=Path(directory);books=root/'books';books.mkdir();shutil.copyfile(fixture,books/'reader.txt')
    saved=os.environ.get('PN_ROOT_RACE_ARTIFACTS');artifacts=Path(saved) if saved else root/'artifacts';artifacts.mkdir(parents=True,exist_ok=True)
    def run(name,script,feedback):
        frames=artifacts/name;frames.mkdir();state=root/(name+'-state');state.mkdir()
        result=subprocess.run([sys.argv[1],'--library',str(books),'--state-dir',str(state)],capture_output=True,text=True,timeout=60,env={**os.environ,'SDL_VIDEODRIVER':'dummy','PN_SIM_LIBRARY_SCRIPT':'home,'+script+',close-window','PN_SIM_PRESENT_DELAY_MS':'500','PN_SIM_CAPTURE_CANCELLED':'1','PN_SIM_CLOCK':'12:34','PN_SIM_TIMING':'1','PN_SIM_FEEDBACK':str(feedback),'PN_SIM_FRAME_DIR':str(frames)})
        (artifacts/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf8')
        assert result.returncode==0 and 'used=0 live=0' in result.stdout,(name,result.stdout[-4000:],result.stderr[-4000:])
        return result.stdout,sorted(frames.glob('frame-*.pgm'))[-1].read_bytes(),result.stderr
    centers=(80,250,420,600)
    for feedback in (0,1):
        for initial,first,second in ((0,3,1),(1,2,0),(2,1,3),(3,0,2)):
            prefix='' if not initial else f'tap:{centers[initial]}:1150,'
            a,b=centers[first],centers[second];stem=f'{feedback}-{initial}-{first}-{second}'
            serial,pixels,_=run(stem+'-serial',prefix+f'tap:{a}:1150,tap:{b}:1150',feedback)
            raced,actual,trace=run(stem+'-racing',prefix+f'race-root:200:{a}:1150:{b}:1150',feedback)
            pending=[line for line in trace.splitlines() if 'root-race' in line]
            assert len(pending)==2 and all('pending=1' in line for line in pending),(stem,trace)
            assert raced.count('navigation press hit=')==serial.count('navigation press hit=')==2+bool(initial),(stem,raced,serial)
            assert actual==pixels,stem
        serial,pixels,_=run(f'{feedback}-content-serial','tap:600:1150',feedback)
        raced,actual,trace=run(f'{feedback}-content-racing','race-root:200:600:1150:100:700',feedback)
        assert raced.count('navigation press hit=')==1 and 'settings command=6' not in raced and actual==pixels,(raced,trace)
print('root queue: four roots, two feedback modes, pending-scan second contact and exact serial frames; stale content still rejected')
