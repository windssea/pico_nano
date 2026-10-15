"""用户EPUB独页封面的真实渲染检查，原件只读。 / Validate standalone-cover rendering with supplied EPUBs, keeping originals read-only."""
import argparse,json,os,pathlib,re,shutil,subprocess,tempfile
from test_local_samples import ROOT,digest

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--capture',type=pathlib.Path,required=True);parser.add_argument('--baseline',type=pathlib.Path);parser.add_argument('--output',type=pathlib.Path,default=ROOT/'build-dev/local-cover-fit');args=parser.parse_args()
    books=sorted(p for p in (ROOT/'tests/testBook').rglob('*') if p.is_file() and p.suffix.lower()=='.epub');font=ROOT/'tests/testFont/霞鹜文楷.ttf';assert books and font.is_file()
    identities={p:digest(p) for p in [*books,font]};args.output.mkdir(parents=True,exist_ok=True);report={'passed':False,'pages':[],'scope':'Actual supplied EPUBs, host complete frames and decoder geometry; no physical panel acceptance'}
    with tempfile.TemporaryDirectory() as temporary:
        root=pathlib.Path(temporary);local_font=root/font.name;shutil.copy2(font,local_font)
        for index,book in enumerate(books):
            local=root/book.name;shutil.copy2(book,local)
            for page in (1,2,3):
                frames={};logs={}
                for label,binary in [('before',args.baseline),('after',args.capture)]:
                    if not binary:continue
                    target=args.output/f'{index}-{page}-{label}.pgm';r=subprocess.run([str(binary.resolve()),str(local),str(local_font),str(page),str(target.resolve()),'44'],env=dict(os.environ,PN_EPUB_IMAGE_RECTS='1'),capture_output=True,text=True,timeout=120)
                    (target.with_suffix('.log')).write_text(r.stdout+r.stderr,encoding='utf8');assert r.returncode==0 and 'used=0 live=0' in r.stderr,(book.name,page,label,r.stderr[-2500:]);frames[label]=target.read_bytes().split(b'\n',3)[3];logs[label]=r
                matches=re.findall(r'image_rect path=(.*?) x=(\d+) y=(\d+) w=(\d+) h=(\d+) viewport_w=(\d+) viewport_h=(\d+) native_w=(\d+) native_h=(\d+) status=(\d+)',logs['after'].stderr)
                changed=args.baseline is not None and frames['before']!=frames['after'];centered=False
                if args.baseline:
                    assert logs['before'].stdout==logs['after'].stdout and frames['before'][1144*684:]==frames['after'][1144*684:],(book.name,page,'progress/footer changed')
                if changed or (book.name.startswith('冷到下雪') and page==1):
                    # 捕获器到达目标页前也实际绘制前页；最后一次图像调用才属于当前封面。
                    # The capture tool also paints preceding pages on the way to its target; the last image call belongs to the current cover.
                    assert matches,(book.name,page,matches);x,y,w,h,vw,vh,nw,nh,status=map(int,matches[-1][1:]);assert not status and nw and nh
                    ew=vw;eh=nh*ew//nw
                    if eh>vh:eh=vh;ew=nw*eh//nh
                    assert (x,y,w,h)==((vw-ew)//2,(vh-eh)//2,ew,eh),(book.name,page,matches);centered=True
                peak=int(re.search(r'EPUB app status=0 peak=(\d+)',logs['after'].stderr)[1]);assert peak<=6*1024*1024
                report['pages'].append({'book':book.name,'page':page,'changed':changed,'centered_native_aspect':centered,'pool_peak':peak,'footer_and_position_equal':True if args.baseline else None,'closed_pool_zero':True,'image_rects':matches})
                print(f'PASS cover fit {book.name} page={page} changed={changed} centered={centered} peak={peak}',flush=True)
    report['originals_unchanged']=all(digest(p)==sha for p,sha in identities.items());assert report['originals_unchanged'];report['passed']=True
    (args.output/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')

if __name__=='__main__':main()
