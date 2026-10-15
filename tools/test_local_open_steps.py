"""用户EPUB/字体副本的部分SHA接续与完整帧对照；固定文件port不是TF/面板计时。 / Partial SHA continuation and complete frames on user copies; immutable ports are not hardware timing."""
import argparse,hashlib,json,os,shutil,subprocess,tempfile
from pathlib import Path
from test_local_samples import ROOT,digest

def fields(line): return {k:int(v) for k,v in (p.split('=') for p in line.split()[1:])}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,required=True);p.add_argument('--baseline',type=Path)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--limit',type=int)
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True);baseline=a.baseline or a.binary
    books=sorted(x for x in (ROOT/'tests/testBook').rglob('*') if x.is_file() and x.suffix.lower()=='.epub')
    fonts=sorted(x for x in (ROOT/'tests/testFont').rglob('*') if x.is_file() and x.suffix.lower()=='.ttf')
    originals={str(x.relative_to(ROOT)):digest(x) for folder in ('testBook','testFont') for x in (ROOT/'tests'/folder).rglob('*') if x.is_file()}
    assert books and fonts
    report={'passed':False,'scope':__doc__,'subset':bool(a.limit),'comparison':'independent_product' if digest(baseline)!=digest(a.binary) else 'same_binary_cold_vs_partial','binary_sha256':digest(a.binary),'baseline_sha256':digest(baseline),'originals':originals,'runs':[]}
    (a.output/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    with tempfile.TemporaryDirectory() as temp:
        root=Path(temp)
        for fi,font in enumerate(fonts):
            copied_font=root/f'font-{fi}.ttf';shutil.copy2(font,copied_font)
            for bi,book in enumerate(books[:a.limit] if a.limit else books):
                copied_book=root/f'book-{bi}.epub';shutil.copy2(book,copied_book)
                for primary in (False,True):
                    case=f'{fi}-{bi}-'+('primary' if primary else 'fallback');results={}
                    for label,binary in [('before',baseline),('after',a.binary)]:
                        state=root/(case+'-'+label);state.mkdir();frames=a.output.resolve()/(case+'-'+label);frames.mkdir(exist_ok=True)
                        env={**os.environ}
                        for key in ('FAULT','PRIMARY','WARM','STAGED','PAINT','INTERRUPT','SOURCE_ONLY','PARTIAL'):env.pop('PN_FONT_STARTUP_'+key,None)
                        env.update(PN_FONT_STARTUP_SYSTEM='1',PN_FONT_STARTUP_FRAMES=str(frames))
                        if primary:env['PN_FONT_STARTUP_PRIMARY']='1'
                        if label=='after':env.update(PN_FONT_STARTUP_WARM='1',PN_FONT_STARTUP_PARTIAL='1')
                        r=subprocess.run([str(binary.resolve()),str(copied_book),str(copied_font),str(state)],env=env,capture_output=True,text=True,timeout=180)
                        (a.output/(case+'-'+label+'.log')).write_text(r.stdout+r.stderr,encoding='utf8')
                        assert r.returncode==0,(case,label,r.stdout[-2500:],r.stderr[-2500:])
                        lines=r.stdout.splitlines();metrics=fields(next(x for x in lines if x.startswith('startup ')))
                        assert metrics['status']==metrics['used']==metrics['live']==metrics['full_hashes']==metrics['font_get_hashes']==0
                        assert metrics['book_hashes']==metrics['book_opens']==(1 if label=='before' else 0) and metrics['font_opens']==1
                        layout={x.split()[0]:int(x.split('calls=')[1]) for x in lines if x.startswith(('idle_layout calls=','click_layout calls='))}
                        assert layout==({'click_layout':1} if label=='before' else {'idle_layout':0,'click_layout':1})
                        hashes=[]
                        for i in range(3):
                            frame=frames/f'frame-{i}.raw';assert frame.stat().st_size==415872;hashes.append(digest(frame))
                        assert len(list(frames.glob('frame-*.raw')))==metrics['presents']==3
                        results[label]={'metrics':metrics,'frames':hashes,'layout':layout,'turn_io':next(x for x in lines if x.startswith('turn_io '))}
                        if label=='after':
                            idle=fields(next(x for x in lines if x.startswith('idle_prepare ')));progress=fields(next(x for x in lines if x.startswith('idle_stream ')));click=fields(next(x for x in lines if x.startswith('click_stream ')))
                            target=65536 if book.stat().st_size>65536 else 32768 if book.stat().st_size>32768 else 0
                            assert progress['bytes']==progress['work_bytes']==target and progress['total']==book.stat().st_size and progress['cancelled']==1
                            assert click['work_bytes']==book.stat().st_size-target and click['work_bytes']>0
                            assert idle['book_hashes']==1 and idle['font_hashes']==idle['state_writes']==0
                            results[label].update(idle=idle,progress=progress,click=click)
                    assert results['before']['frames']==results['after']['frames'] and results['before']['turn_io']==results['after']['turn_io']
                    for i in range(3):assert (a.output/(case+'-before')/f'frame-{i}.raw').read_bytes()==(a.output/(case+'-after')/f'frame-{i}.raw').read_bytes()
                    report['runs'].append({'book':book.name,'font':font.name,'primary':primary,**results})
                    print('PASS partial open',case,book.name,font.name,flush=True)
    assert all(digest(ROOT/name)==value for name,value in originals.items())
    report['originals_unchanged']=report['passed']=True
    (a.output/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')

if __name__=='__main__':main()
