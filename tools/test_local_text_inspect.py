"""只读核查用户TXT的一遍扫描、完整SHA与检查点。 / Read-only supplied-TXT verification of one scan, full SHA and checkpoints."""
import argparse,json,pathlib,re,subprocess
from test_local_samples import ROOT,digest

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=pathlib.Path,required=True)
    parser.add_argument('--output',type=pathlib.Path,default=ROOT/'build-dev/local-text-inspect')
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    books=sorted((ROOT/'tests/testBook').rglob('*.txt'));assert books
    hashes={p:digest(p) for p in books};report={'passed':False,'books':[],'scope':'Host shared source; CPU and file bytes are not physical TF/panel measurements'}
    for i,p in enumerate(books):
        result=subprocess.run([str(args.binary.resolve()),str(p)],capture_output=True,text=True,timeout=120)
        (args.output/f'{i}.log').write_text(result.stdout+result.stderr,encoding='utf8');assert result.returncode==0,(p.name,result.stdout,result.stderr)
        line=next(l for l in result.stdout.splitlines() if l.startswith('inspect '));fields=dict(re.findall(r'(\w+)=([^\s]+)',line))
        assert fields['sha256']==hashes[p] and int(fields['input'])==p.stat().st_size and int(fields['bytes'])<=p.stat().st_size+8192,fields
        report['books'].append({'book':p.name,**fields});print('PASS text inspect',p.name,fields,flush=True)
    report['originals_unchanged']=all(digest(p)==h for p,h in hashes.items());assert report['originals_unchanged'];report['passed']=True
    (args.output/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')

if __name__=='__main__':main()
