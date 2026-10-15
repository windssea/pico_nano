"""完整用户文件的增量硬件SHA控制流对照；PSA/DMA替身不代表设备RAM/TF时间。 / Incremental hardware SHA control flow on user files; PSA/DMA doubles are not device RAM/TF timing."""
import argparse,json,math,os,shutil,subprocess,tempfile
from pathlib import Path
from test_local_samples import ROOT,digest

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--baseline',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args();baseline=a.baseline or a.binary;a.output.mkdir(parents=True,exist_ok=True)
    files=sorted(x for folder in ('testBook','testFont') for x in (ROOT/'tests'/folder).rglob('*') if x.is_file());assert files
    originals={str(x.relative_to(ROOT)):digest(x) for x in files}
    report={'passed':False,'scope':__doc__,'comparison':'independent_product' if digest(baseline)!=digest(a.binary) else 'same_binary_sync_vs_job','binary_sha256':digest(a.binary),'baseline_sha256':digest(baseline),'originals':originals,'runs':[]}
    (a.output/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    with tempfile.TemporaryDirectory() as temp:
        for i,source in enumerate(files):
            copied=Path(temp)/f'{i}.bin';shutil.copy2(source,copied);results={}
            for label,binary in [('before',baseline),('after',a.binary)]:
                env={**os.environ};env.pop('PN_IDENTITY_BORROWED',None)
                command=[str(binary.resolve()),str(copied),'65536']+(['job-cancel'] if label=='after' else [])
                r=subprocess.run(command,env=env,capture_output=True,text=True,timeout=180);(a.output/f'{i}-{label}.log').write_text(r.stdout+r.stderr,encoding='utf8');assert r.returncode==0,(source,label,r.stderr)
                line=next(x for x in r.stdout.splitlines() if x.startswith('file '));v=dict(p.split('=') for p in line.split()[1:]);assert v.pop('digest')==originals[str(source.relative_to(ROOT))]
                results[label]={k:int(v) for k,v in v.items()}
            old,new=results['before'],results['after'];size=source.stat().st_size
            assert old['bytes']==new['bytes']==size and old['reads']==new['reads']==math.ceil(size/32768)
            assert old['maximum']==new['maximum']==min(size,32768)
            assert old['attempts']==old['peak']==0 and new['attempts']==1 and 0<new['peak']<=1024
            report['runs'].append({'source':str(source.relative_to(ROOT)),**results});print('PASS incremental hardware identity',source.name,flush=True)
    assert all(digest(ROOT/name)==value for name,value in originals.items());report['originals_unchanged']=report['passed']=True
    (a.output/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')

if __name__=='__main__':main()
