"""用户书库三/四列滑动、封面冷暖缓存与菜单往返。 / Supplied library in three/four columns: swipes, cold/warm covers and root navigation."""
import argparse,json,os,pathlib,re,shutil,subprocess,tempfile
from test_local_samples import ROOT,digest

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--sim',type=pathlib.Path,required=True);parser.add_argument('--output',type=pathlib.Path,default=ROOT/'build-dev/local-shelf');args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    books=sorted(p for p in (ROOT/'tests/testBook').rglob('*') if p.is_file());fonts=sorted((ROOT/'tests/testFont').glob('*.ttf'));assert books and fonts
    hashes={p:digest(p) for p in books+fonts};report={'passed':False,'runs':[],'scope':'Host shared UI/pool and file cache; physical panel/TF/touch remain hardware validation'}
    with tempfile.TemporaryDirectory() as temp:
        root=pathlib.Path(temp);library=root/'books';library.mkdir();fontdir=root/'fonts';fontdir.mkdir()
        for p in books:shutil.copy2(p,library/p.name)
        for p in fonts:shutil.copy2(p,fontdir/p.name)
        font=fontdir/'霞鹜文楷.ttf';assert font.is_file()
        count=sum(p.suffix.lower() in ('.txt','.epub','.pdf','.fb2','.cbz') for p in books)
        for cols in (3,4):
            state=root/f'state-{cols}';cache=root/f'cache-{cols}';cache.mkdir();per=cols*3
            for warm in (False,True):
                name=f'{cols}-'+('warm' if warm else 'cold');wait=1800 if warm else 6500
                appearance='settings,tap:100:450,tap:550:240,back,back,' if cols==4 and not warm else ''
                script='library,'+appearance+f'wait:{wait},press:550:650,move:150:650,release:150:650,wait:{wait},press:150:650,move:550:650,release:550:650,tap:600:1150,tap:80:1150,tap:250:1150,wait:{wait},close-window'
                frames=args.output/(name+'-frames');frames.mkdir(exist_ok=True)
                result=subprocess.run([str(args.sim.resolve()),'--library',str(library),'--font',str(font),'--font-dir',str(fontdir),'--state-dir',str(state),'--cover-cache',str(cache)],env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT=script,PN_SIM_CLOCK='12:34',PN_SIM_FRAME_DIR=str(frames.resolve())),capture_output=True,text=True,timeout=120)
                (args.output/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf8')
                assert result.returncode==0 and 'used=0 live=0' in result.stdout,(name,result.stdout[-2500:],result.stderr[-2000:])
                assert f'navigation root=library count={min(count,per)}' in result.stdout and f'shelf swipe command=41 index={per} status=0' in result.stdout and 'shelf swipe command=40 index=0 status=0' in result.stdout,result.stdout
                assert 'library_open' not in result.stdout and 'settings open status=0' in result.stdout,result.stdout
                files={p.name:digest(p) for p in cache.glob('*.pnc')};assert len(files)==count,(name,len(files),count,result.stdout)
                if warm:assert files==cold and 'covers ready=' not in result.stdout,(name,result.stdout)
                else:cold=files
                peak=int(re.search(r'library_window pool_peak=(\d+)',result.stdout)[1]);assert peak<=6*1024*1024
                report['runs'].append({'columns':cols,'warm':warm,'books':count,'cover_records':len(files),'pool_peak':peak,'frames':len(list(frames.glob('*.pgm'))),'cache_bytes_unchanged':warm})
                print('PASS shelf',name,report['runs'][-1],flush=True)
    report['originals_unchanged']=all(digest(p)==h for p,h in hashes.items());assert report['originals_unchanged'];report['passed']=True
    (args.output/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')

if __name__=='__main__':main()
