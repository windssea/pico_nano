"""用户整库/字体副本的根页密集点击和失焦持有检查，不测面板时间。 / Check dense root navigation and focus holds with supplied library/font copies, without panel timing."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from test_local_samples import ROOT,digest


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sim',type=Path,required=True)
    parser.add_argument('--baseline',type=Path,help='Optional frozen probe built with the same race-roots/race-focus script support')
    parser.add_argument('--output',type=Path,default=ROOT/'build-dev/local-root-navigation')
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    books=sorted(p for p in (ROOT/'tests/testBook').rglob('*') if p.is_file())
    fonts=sorted(p for p in (ROOT/'tests/testFont').rglob('*') if p.is_file() and p.suffix.lower()=='.ttf')
    assert books and fonts and len({p.name for p in books})==len(books)
    original={p:digest(p) for p in books+fonts};report={'passed':False,'scope':'Actual menus with supplied library and fonts, dense roots and held focus contacts; simulated scan waits are not physical TF/panel latency and no reading compatibility is claimed.','books':[p.name for p in books],'runs':[]}
    output=args.output/'report.json'
    with tempfile.TemporaryDirectory() as directory:
        root=Path(directory);library=root/'books';installed=root/'fonts';library.mkdir();installed.mkdir()
        for p in books:shutil.copy2(p,library/p.name)
        for p in fonts:shutil.copy2(p,installed/p.name)
        for fi,font in enumerate(fonts):
            for kind,script,serial in (('dense','race-roots:200:3:1:2:0','tap:600:1150,tap:80:1150'),('focus','race-focus:200:600:1150:80:1150','tap:600:1150')):
                def run(binary,label,commands):
                    stem=f'{fi}-{kind}-{label}';state=root/stem;state.mkdir();cache=root/(stem+'-cache');cache.mkdir();frames=args.output/stem;frames.mkdir()
                    result=subprocess.run([str(binary.resolve()),'--library',str(library),'--state-dir',str(state),'--cover-cache',str(cache),'--font',str(installed/font.name),'--font-dir',str(installed)],capture_output=True,text=True,timeout=90,env={**os.environ,'SDL_VIDEODRIVER':'dummy','PN_SIM_LIBRARY_SCRIPT':'home,'+commands+',wait:1200,close-window','PN_SIM_CLOCK':'12:34','PN_SIM_PRESENT_DELAY_MS':'700','PN_SIM_TIMING':'1','PN_SIM_CAPTURE_CANCELLED':'1','PN_SIM_FEEDBACK':'0','PN_SIM_FRAME_DIR':str(frames.resolve())})
                    (args.output/(stem+'.log')).write_text(result.stdout+result.stderr,encoding='utf8')
                    assert result.returncode==0 and 'used=0 live=0' in result.stdout,(stem,result.stdout[-3000:],result.stderr[-3000:])
                    captures=sorted(frames.glob('*.pgm'));assert captures and all(p.stat().st_size==831760 for p in captures)
                    pending=[line for line in result.stderr.splitlines() if 'root-race' in line]
                    if label!='serial':assert len(pending)==(6 if kind=='dense' else 2) and all('pending=1' in line for line in pending),(stem,pending)
                    return {'navigation_presses':result.stdout.count('navigation press hit='),'last_frame':str(captures[-1].relative_to(args.output)),'last_sha256':digest(captures[-1]),'frames':len(captures),'pending_events':len(pending),'idle_index_completed':'catalog index built' in result.stdout}
                expected=run(args.sim,'serial',serial);new=run(args.sim,'after',script)
                assert new['navigation_presses']==expected['navigation_presses']==(2 if kind=='dense' else 1)
                if kind=='dense':assert new['idle_index_completed'] and expected['idle_index_completed']
                assert (args.output/new['last_frame']).read_bytes()==(args.output/expected['last_frame']).read_bytes()
                before=run(args.baseline,'before',script) if args.baseline else None
                report['runs'].append({'font':font.name,'kind':kind,'serial':expected,'after':new,'before':before,'final_frame_matches_serial':True})
                output.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8');print('PASS root navigation',font.name,kind,flush=True)
    report['originals_unchanged']=all(digest(p)==identity for p,identity in original.items());assert report['originals_unchanged']
    report['passed']=True;output.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')


if __name__=='__main__':main()
