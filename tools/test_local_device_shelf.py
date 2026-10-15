"""隔离Docker中的用户书库设备手势旅程。 / Device gesture journeys with the supplied library inside isolated Docker."""
import argparse,json,pathlib,re,shutil,subprocess
from test_local_samples import ROOT,digest


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=pathlib.Path,required=True)
    parser.add_argument('--output',type=pathlib.Path,default=ROOT/'build-dev/local-device-shelf')
    args=parser.parse_args()
    # 固定设备路径只在一次性容器内创建，绝不映射用户TF。/ Create fixed device paths only in a disposable container, never mapping the user's TF.
    if not pathlib.Path('/.dockerenv').is_file():raise ValueError('Requires a fresh disposable Docker container')
    library=pathlib.Path('/sdcard/piconano/books');assert not library.exists();library.mkdir(parents=True)
    originals=sorted(p for p in (ROOT/'tests/testBook').rglob('*') if p.is_file());assert originals
    assert len({p.name for p in originals})==len(originals)
    identities={p:digest(p) for p in originals}
    for p in originals:shutil.copy2(p,library/p.name)
    args.output.mkdir(parents=True,exist_ok=True)
    report={'passed':False,'runs':[],'scope':'Real device shelf routing and normalized driver reports with SDK/hardware substitutes; no physical touch/panel acceptance'}
    target=args.output/'report.json';target.write_text(json.dumps(report),encoding='utf8')
    for columns in (3,4,5):
        result=subprocess.run([str(args.binary.resolve()),str(columns)],capture_output=True,text=True,timeout=120)
        (args.output/f'{columns}.log').write_text(result.stdout+result.stderr,encoding='utf8')
        match=re.search(r'device_shelf columns=(\d+) capacity=(\d+) raw_up_swipe=1 diagonal_swipe=1 ordinary_swipe=1 edges_cancel=1 position_restored=1 used=0 live=0 peak=(\d+)',result.stdout)
        assert result.returncode==0 and match,(columns,result.returncode,result.stdout[-2000:],result.stderr[-2000:])
        report['runs'].append({'columns':columns,'capacity':int(match[2]),'pool_peak':int(match[3]),'raw_up_swipe':True,'diagonal_swipe':True,'ordinary_swipe':True,'edges_cancel':True,'position_restored':True,'closed_pool_zero':True})
        print('PASS device shelf',report['runs'][-1],flush=True)
    report['originals_unchanged']=all(digest(p)==sha for p,sha in identities.items());assert report['originals_unchanged'];report['passed']=True
    target.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')


if __name__=='__main__':main()
