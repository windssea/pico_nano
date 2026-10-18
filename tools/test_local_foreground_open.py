"""用户EPUB/字体副本驱动真实设备冷开控制器；硬件替身不代表TF/面板时延。 / Real device cold-opening controller on user EPUB/font copies; hardware substitutes are not TF/panel timing."""
import argparse,json,os,shutil,subprocess
from pathlib import Path
from test_local_samples import ROOT,digest

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--baseline',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--limit',type=int);p.add_argument('--scenarios',action='store_true');a=p.parse_args()
    assert Path('/.dockerenv').is_file(),'Requires a fresh disposable container'
    library=Path('/sdcard/piconano/books');font=Path('/sdcard/piconano/fonts/HarmonyOS_Sans_SC_Regular.ttf');state=Path('/data/progress')
    assert not library.exists() and not font.exists() and not state.exists()
    library.mkdir(parents=True);font.parent.mkdir(parents=True);state.mkdir(parents=True)
    originals={str(x.relative_to(ROOT)):digest(x) for folder in ('testBook','testFont') for x in (ROOT/'tests'/folder).rglob('*') if x.is_file()}
    books=sorted(x for x in (ROOT/'tests/testBook').rglob('*') if x.is_file());fonts=sorted(x for x in (ROOT/'tests/testFont').rglob('*') if x.is_file() and x.suffix.lower()=='.ttf')
    for source in books:shutil.copy2(source,library/source.name)
    epubs=[x for x in books if x.suffix.lower()=='.epub'];assert epubs and fonts
    a.output.mkdir(parents=True,exist_ok=True)
    report={'passed':False,'scope':__doc__,'subset':bool(a.limit),'originals':originals,'binary_sha256':digest(a.binary),'baseline_sha256':digest(a.baseline) if a.baseline else None,'comparison':'independent_producers' if a.baseline and digest(a.baseline)!=digest(a.binary) else 'current_only','runs':[],'scenarios':[]}
    target=a.output/'report.json';target.write_text(json.dumps(report),encoding='utf8')
    for fi,source_font in enumerate(fonts):
        shutil.copy2(source_font,font)
        for bi,source_book in enumerate(epubs[:a.limit] if a.limit else epubs):
            results={}
            for label,binary in ([('before',a.baseline)] if a.baseline else [])+[('after',a.binary)]:
                assert state==Path('/data/progress') and state.parent==Path('/data');shutil.rmtree(state);state.mkdir()
                frames=a.output.resolve()/f'{fi}-{bi}-{label}';frames.mkdir(exist_ok=True)
                env={**os.environ};[env.pop(k,None) for k in ('PN_MENU_FRAMES','PN_MENU_PREPARE_BOOKS','PN_MENU_CANCEL_SYSTEM_PROBE','PN_MENU_SYSTEM_FAILURE_PROBE','PN_OPEN_SCENARIO')]
                env.update(PN_MENU_COLD_BOOK=str(library/source_book.name),PN_OPEN_FRAMES=str(frames))
                r=subprocess.run([str(binary.resolve())],env=env,capture_output=True,text=True,timeout=180);(a.output/f'{fi}-{bi}-{label}.log').write_text(r.stdout+r.stderr,encoding='utf8')
                assert r.returncode==0 and 'menu_probe closed_pool_zero=1' in r.stdout,(source_book.name,label,r.stdout[-2000:],r.stderr[-2000:])
                line=next(x for x in r.stdout.splitlines() if x.startswith('cold_open '));metrics={k:int(v) for k,v in (word.split('=') for word in line.split()[1:])}
                assert metrics['body_once']==metrics['loader_once']==1 and metrics['status']==0 and (metrics['polls']==0 if label=='before' else metrics['polls']>0)
                assert len(list(frames.glob('*.raw')))==3
                results[label]={'metrics':metrics,'frames':[digest(frames/f'{i}.raw') for i in range(3)]}
                assert all((frames/f'{i}.raw').stat().st_size==415872 for i in range(3))
            if a.baseline:
                assert results['before']['frames']==results['after']['frames']
                for i in range(3):assert (a.output/f'{fi}-{bi}-before'/f'{i}.raw').read_bytes()==(a.output/f'{fi}-{bi}-after'/f'{i}.raw').read_bytes()
            report['runs'].append({'book':source_book.name,'font':source_font.name,**results});print('PASS foreground cold open',fi,bi,source_book.name,flush=True)
        if a.scenarios:
            for case in ('cancel-home','cancel-back','cancel-scope','lock','stale','usb','display-fail','wrong-id'):
                assert state==Path('/data/progress') and state.parent==Path('/data');shutil.rmtree(state);state.mkdir()
                env={**os.environ,'PN_MENU_COLD_BOOK':str(library/epubs[0].name),'PN_OPEN_SCENARIO':case}
                r=subprocess.run([str(a.binary.resolve())],env=env,capture_output=True,text=True,timeout=180);(a.output/f'{fi}-{case}.log').write_text(r.stdout+r.stderr,encoding='utf8')
                assert r.returncode==0 and 'menu_probe closed_pool_zero=1' in r.stdout,(case,r.stdout[-2000:],r.stderr[-2000:])
                line=next(x for x in r.stdout.splitlines() if x.startswith('cold_cancel '));fields=dict(word.split('=') for word in line.split()[1:]);assert fields['case']==case and fields['no_record']==fields['released']=='1'
                report['scenarios'].append({'font':source_font.name,**fields});print('PASS foreground interruption',fi,case,flush=True)
            # 切换EPUB：读A→回书库→空闲准备/身份预计算→开B→最近记录重开A→加载页扫描期间即完成的开书。/ Switching EPUBs: read A, back, idle prepare/identity precompute, open B, reopen A from recents, then an open finishing during the loading scan.
            if len(epubs)>=2:
                shutil.rmtree(state);state.mkdir()
                env={k:v for k,v in os.environ.items() if k not in ('PN_MENU_COLD_BOOK','PN_OPEN_SCENARIO')};env.update(PN_SWITCH_A=str(library/epubs[0].name),PN_SWITCH_B=str(library/epubs[1].name))
                r=subprocess.run([str(a.binary.resolve())],env=env,capture_output=True,text=True,timeout=600);(a.output/f'{fi}-switch.log').write_text(r.stdout+r.stderr,encoding='utf8')
                assert r.returncode==0 and 'switch_probe ok' in r.stdout and 'overlap_open steps=' in r.stdout,(r.stdout[-2000:],r.stderr[-2000:])
                report['scenarios'].append({'font':source_font.name,'case':'switch'});print('PASS foreground switch',fi,flush=True)
    assert all(digest(ROOT/name)==value for name,value in originals.items());report['passed']=report['originals_unchanged']=True
    target.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')

if __name__=='__main__':main()
