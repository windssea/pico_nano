"""用户整库封面内存复用与旧PNCV路径的逐帧对照。 / Compare resident covers and legacy PNCV loading with the supplied library."""
import argparse,json,pathlib,re,shutil,subprocess,tempfile
from test_local_samples import ROOT,digest

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--binary',type=pathlib.Path,required=True);parser.add_argument('--output',type=pathlib.Path,default=ROOT/'build-dev/local-cover-resident');args=parser.parse_args()
    originals=sorted(p for p in (ROOT/'tests/testBook').rglob('*') if p.is_file() and p.suffix.lower() in ('.txt','.epub'))
    assert originals;identities={p:digest(p) for p in originals};args.output.mkdir(parents=True,exist_ok=True)
    report={'passed':False,'cases':[],'scope':'Actual supplied covers, file-interface I/O and full host frame comparison; not physical TF/panel timings'}
    with tempfile.TemporaryDirectory() as temporary:
        root=pathlib.Path(temporary);peaks=[]
        # 每组固定槽容量，新增书籍自动进入下一组；原书始终只读。/ Respect fixed slot capacity and include newly added books in subsequent batches; originals stay read-only.
        for first in range(0,len(originals),13):
            batch=first//13;folder=root/str(batch);folder.mkdir();cache=folder/'cache';cache.mkdir();copies=[]
            for index,p in enumerate(originals[first:first+13]):
                local=folder/f'{index}-{p.name}';shutil.copy2(p,local);copies.append(local)
            result=subprocess.run([str(args.binary.resolve()),str(cache),*map(str,copies)],capture_output=True,text=True,timeout=300)
            (args.output/f'raw-{batch}.log').write_text(result.stdout+result.stderr,encoding='utf8')
            assert result.returncode==0,(batch,result.returncode,result.stdout[-2500:],result.stderr[-2500:])
            assert 'cache_probe first_bind_opens=0 first_bind_source_stats=0 max_files_per_tick=1' in result.stdout
            report['cold_bind_cache_reads']=0;report['cold_bind_source_stats']=0;report['max_cache_files_per_tick']=1
            pattern=r'resident case=([^ ]+) legacy_opens=(\d+) legacy_bytes=(\d+) resident_opens=(\d+) resident_bytes=(\d+) exact_frames=1 legacy_stats=(\d+) resident_stats=(\d+)'
            rows=[]
            for name,old_opens,old_bytes,new_opens,new_bytes,old_stats,new_stats in re.findall(pattern,result.stdout):
                row=dict(batch=batch,case=name,legacy_opens=int(old_opens),legacy_bytes=int(old_bytes),resident_opens=int(new_opens),resident_bytes=int(new_bytes),legacy_stats=int(old_stats),resident_stats=int(new_stats),exact_frames=True);rows.append(row);print(row,flush=True)
            assert len(rows)==41 and all(x['resident_bytes']==0 and x['resident_opens']==0 for x in rows[:5]);report['cases'].extend(rows)
            end=re.search(r'resident done covers_bytes=(\d+) peak=(\d+) used=0 live=0 cancellation=1 usb_fence=1 source_change=1',result.stdout);assert end;peaks.append(int(end[2]))
        report.update(books=len(originals),covers_bytes=int(end[1]),pool_peak=max(peaks),closed_pool_zero=True,cancellation=True,usb_fence=True,source_change=True)
    report['originals_unchanged']=all(digest(p)==sha for p,sha in identities.items());assert report['originals_unchanged'];report['passed']=True
    (args.output/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')

if __name__=='__main__':main()
