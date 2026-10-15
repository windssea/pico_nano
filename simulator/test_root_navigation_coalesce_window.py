"""根页密集点击只准备最后目标；失焦后的同一持有不能制造新导航。 / Dense root clicks prepare only the last target; the same hold after focus loss cannot create navigation."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

fixture=Path(__file__).resolve().parents[1]/'tests/fixtures/reader_sample.txt'
with tempfile.TemporaryDirectory() as directory:
    root=Path(directory);books=root/'books';books.mkdir();shutil.copyfile(fixture,books/'reader.txt')
    saved=os.environ.get('PN_ROOT_COALESCE_ARTIFACTS');artifacts=Path(saved) if saved else root/'artifacts';artifacts.mkdir(parents=True,exist_ok=True)
    def run(name,script,feedback):
        state=root/(name+'-state');state.mkdir();cache=root/(name+'-cache');cache.mkdir();frames=artifacts/name;frames.mkdir()
        result=subprocess.run([sys.argv[1],'--library',str(books),'--state-dir',str(state),'--cover-cache',str(cache)],capture_output=True,text=True,timeout=60,env={**os.environ,'SDL_VIDEODRIVER':'dummy','PN_SIM_LIBRARY_SCRIPT':'home,'+script+',close-window','PN_SIM_CLOCK':'12:34','PN_SIM_PRESENT_DELAY_MS':'700','PN_SIM_TIMING':'1','PN_SIM_CAPTURE_CANCELLED':'1','PN_SIM_FEEDBACK':str(feedback),'PN_SIM_FRAME_DIR':str(frames)})
        (artifacts/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf8')
        assert result.returncode==0 and 'used=0 live=0' in result.stdout,(name,result.stdout[-4000:],result.stderr[-4000:])
        return result.stdout,sorted(frames.glob('*.pgm'))[-1].read_bytes(),result.stderr
    for feedback in (0,1):
        for initial in range(4):
            prefix='' if not initial else f'tap:{85+171*initial}:1150,'
            first,second,third,last=((initial+i)%4 for i in (1,2,3,4));stem=f'{feedback}-{initial}'
            serial,pixels,_=run(stem+'-serial',prefix+f'tap:{85+171*first}:1150,tap:{85+171*last}:1150,wait:1200',feedback)
            raced,actual,trace=run(stem+'-racing',prefix+f'race-roots:200:{first}:{second}:{third}:{last},wait:1200',feedback)
            pending=[line for line in trace.splitlines() if 'root-race' in line]
            assert len(pending)==6 and all('pending=1' in line for line in pending),(stem,trace)
            assert raced.count('navigation press hit=')==serial.count('navigation press hit=')==2+bool(initial) and 'navigation merged events=5 selections=3' in raced,(stem,raced)
            assert actual==pixels,stem
            if initial==0:assert 'catalog index built' in raced and 'catalog index built' in serial,(stem,raced,serial)
        serial,pixels,_=run(f'{feedback}-focus-serial','tap:600:1150',feedback)
        raced,actual,trace=run(f'{feedback}-focus-racing','race-focus:200:600:1150:80:1150',feedback)
        pending=[line for line in trace.splitlines() if 'root-race' in line]
        assert len(pending)==2 and all('pending=1' in line for line in pending),(raced,trace)
        assert raced.count('navigation press hit=')==1 and actual==pixels,(raced,trace)
print('root coalescing: four roots, two feedback modes, exact final frames and capture-time focus ownership passed')
