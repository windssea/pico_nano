"""用户书籍/字体副本的冷开补字摘要、读取量与完整画面对照。 / Compare cold fallback hashing, reads and full frames with copied user books/fonts."""
import argparse
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import tempfile
from test_local_samples import ROOT, digest


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=pathlib.Path,required=True)
    parser.add_argument('--baseline',type=pathlib.Path)
    parser.add_argument('--output',type=pathlib.Path,default=ROOT/'build-dev/local-fallback-startup')
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    books=sorted(p for p in (ROOT/'tests/testBook').rglob('*') if p.is_file() and p.suffix.lower() in ('.txt','.epub'))
    fonts=sorted(p for p in (ROOT/'tests/testFont').rglob('*') if p.is_file() and p.suffix.lower()=='.ttf')
    assert books and fonts
    originals={p:digest(p) for p in books+fonts}
    report={'passed':False,'scope':'Original user copies, real reader open/NEXT/PREVIOUS; file-port bytes and full frame identities, not physical TF/panel timing','runs':[]}
    report_path=args.output/'report.json';report_path.write_text(json.dumps(report),encoding='utf8')
    with tempfile.TemporaryDirectory() as folder:
        root=pathlib.Path(folder);installed=root/'fonts';installed.mkdir()
        for font in fonts:shutil.copy2(font,installed/font.name)
        for bi,book in enumerate(books):
            target=root/book.name;shutil.copy2(book,target)
            for fi,font in enumerate(fonts):
                def run(binary,label):
                    state=root/f'state-{bi}-{fi}-{label}';state.mkdir()
                    frames=args.output.resolve()/f'{bi}-{fi}-{label}';frames.mkdir()
                    result=subprocess.run([str(binary.resolve()),str(target),str(installed/font.name),str(state)],capture_output=True,text=True,timeout=180,env={**os.environ,'PN_FONT_STARTUP_FRAMES':str(frames)})
                    (args.output/f'{bi}-{fi}-{label}.log').write_text(result.stdout+result.stderr,encoding='utf8')
                    assert result.returncode==0,(book.name,font.name,label,result.stdout[-1800:],result.stderr[-1800:])
                    row=next(line for line in result.stdout.splitlines() if line.startswith('startup '))
                    fields={k:int(v) for k,v in (word.split('=') for word in row.split()[1:])}
                    pixels=re.findall(r'^frame=\d+ digest=([a-f0-9]{64})$',result.stdout,re.M)
                    assert fields['status']==fields['used']==fields['live']==0 and fields['presents']==len(pixels) and pixels
                    if label=='after':
                        captured=sorted(frames.glob('frame-*.raw'))
                        assert len(captured)==len(pixels) and all(p.stat().st_size==415872 for p in captured)
                        assert [hashlib.sha256(p.read_bytes()).hexdigest() for p in captured]==pixels
                        assert fields['full_hashes']==fields['book_hashes']==fields['font_opens']==1 and fields['font_get_hashes']==0,fields
                    return {'metrics':fields,'frames':pixels}
                old=run(args.baseline,'before') if args.baseline else None
                new=run(args.binary,'after')
                if old:
                    assert old['frames']==new['frames'] and old['metrics']['full_hashes']==old['metrics']['font_opens']==2
                    assert old['metrics']['font_read_bytes']>new['metrics']['font_read_bytes']
                report['runs'].append({'book':book.name,'font':font.name,'before':old,'after':new,'frame_identities_match':bool(old)})
                report_path.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
                print('PASS fallback startup',book.name,font.name,flush=True)
    report['originals_unchanged']=all(digest(p)==value for p,value in originals.items());assert report['originals_unchanged']
    report['passed']=True;report_path.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')


if __name__=='__main__':main()
