"""用户原件副本的硬件SHA端口控制流/池缓冲对照；PSA/DMA为替身，不是TF性能或设备内存。 / Hardware-port control flow and pool scratch on user copies; PSA/DMA doubles, not physical TF performance or device RAM."""
import argparse,json,math,os,shutil,subprocess,tempfile
from pathlib import Path
from test_local_samples import ROOT,digest

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--baseline',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
 files=sorted(x for folder in ('testBook','testFont') for x in (ROOT/'tests'/folder).rglob('*') if x.is_file());assert files
 baseline=a.baseline or a.binary
 original={str(x.relative_to(ROOT)):digest(x) for x in files};report={'passed':False,'scope':__doc__,'comparison':'independent_prior_product' if a.baseline else 'same_binary_own_vs_borrowed','binary_sha256':digest(a.binary),'baseline_sha256':digest(baseline),'originals':original,'runs':[]}
 with tempfile.TemporaryDirectory() as directory:
  for i,source in enumerate(files):
   copied=Path(directory)/f'{i}.bin';shutil.copy2(source,copied);size=copied.stat().st_size
   for budget in (65536,1):
    results={}
    for label,binary in [('before',baseline),('after',a.binary)]:
     env={**os.environ};env.pop('PN_IDENTITY_BORROWED',None)
     if label=='before' and not a.baseline:env['PN_IDENTITY_BORROWED']='1'
     r=subprocess.run([str(binary.resolve()),str(copied),str(budget)],capture_output=True,text=True,timeout=180,env=env);(a.output/f'{i}-{budget}-{label}.log').write_text(r.stdout+r.stderr,encoding='utf8');assert r.returncode==0,(source,budget,label,r.stderr)
     line=next(x for x in r.stdout.splitlines() if x.startswith('file '));v=dict(x.split('=') for x in line.split()[1:]);assert v.pop('digest')==original[str(source.relative_to(ROOT))];results[label]={k:int(x) for k,x in v.items()}
    old,new=results['before'],results['after'];assert new['bytes']==old['bytes']==size and new['attempts']==new['peak']==0
    assert old['attempts']==int(size>4096) and old['peak']==(32800 if budget==65536 and size>4096 else 0)
    new_cap=32768 if size>4096 else 4096;old_cap=32768 if budget==65536 and size>4096 else 4096
    assert new['maximum']==min(size,new_cap) and new['reads']==math.ceil(size/new_cap)
    assert old['maximum']==min(size,old_cap) and old['reads']==math.ceil(size/old_cap)
    report['runs'].append({'source':str(source.relative_to(ROOT)),'budget':budget,**results})
   print('PASS identity scratch',source.name,flush=True)
 assert all(digest(ROOT/name)==identity for name,identity in original.items());report['passed']=report['originals_unchanged']=True;(a.output/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
if __name__=='__main__':main()
