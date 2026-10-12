"""本机私人书籍/字体同源回归；原件只读，报告与运行副本不入库。 / Shared regressions for private local books/fonts; originals are read-only and reports/runtime copies stay local."""
import argparse
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET
import zipfile
from test_epub_sample import inspect

ROOT = pathlib.Path(__file__).resolve().parents[1]

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest',type=pathlib.Path,default=ROOT/'build-dev/sim-library/local-inputs.json')
    parser.add_argument('--sim',type=pathlib.Path,required=True)
    parser.add_argument('--probe',type=pathlib.Path,required=True)
    parser.add_argument('--zip-binary',type=pathlib.Path,required=True)
    parser.add_argument('--output',type=pathlib.Path,default=ROOT/'build-dev/local-samples')
    args=parser.parse_args()
    entries=json.loads(args.manifest.read_text(encoding='utf-8'))['files']
    sources=[]
    for entry in entries:
        path=pathlib.Path(entry['path']);path=path if path.is_absolute() else ROOT/path
        sources.append((entry['kind'],path,digest(path)))
    books=[p for kind,p,_ in sources if kind=='book' and p.suffix.lower()=='.epub']
    fonts=[p for kind,p,_ in sources if kind=='font']
    if not books or not fonts:
        raise ValueError('Local profile needs an EPUB and at least one font')
    args.output.mkdir(parents=True,exist_ok=True)
    report={'scope':'Host/SDL only; no panel or power measurements','sources':[{'name':p.name,'kind':kind,'bytes':p.stat().st_size,'sha256':sha} for kind,p,sha in sources],'books':[],'fonts':[]}
    failed=False
    with tempfile.TemporaryDirectory() as temporary:
        root=pathlib.Path(temporary)
        for index,book in enumerate(books):
            library=root/f'books-{index}';library.mkdir();copy=library/book.name;shutil.copy2(book,copy)
            oracle=inspect(copy,args.zip_binary.resolve(),all_resources=True)
            characters=set();malformed=[]
            with zipfile.ZipFile(copy) as archive:
                for name in archive.namelist():
                    if not name.lower().endswith(('.html','.xhtml','.htm')):continue
                    try:
                        document=ET.fromstring(archive.read(name))
                        body=next((node for node in document.iter() if node.tag.rsplit('}',1)[-1]=='body'),None)
                        if body is not None:characters.update(ord(c) for c in ''.join(body.itertext()) if not c.isspace())
                    except ET.ParseError:malformed.append(name)
            points=root/'body-codepoints.txt';points.write_text(''.join(f'{cp:x}\n' for cp in sorted(characters)),encoding='ascii')
            oracle.update({'unique_body_characters':len(characters),'coverage_oracle_malformed_xhtml':malformed})
            report['books'].append(oracle)
            print(f'EPUB PASS {book.name}: {oracle["compared_resources"]} resources, {oracle["spine_items"]} spine items',flush=True)
            # 无font参数先验证内置回退；每个外置字体使用独立状态测试真实续读。
            # First validate built-in fallback without --font; each external font gets isolated state for real restore tests.
            for fi,font in enumerate([None,*fonts]):
                name=font.name if font else 'built-in';runtime_font=None
                runtime_font=root/f'font-{fi}.ttf';shutil.copy2(font or ROOT/"assets/fonts/read-pico-reading.ttf",runtime_font)
                probe=subprocess.run([str(args.probe.resolve()),str(runtime_font),str(points)],capture_output=True,text=True,timeout=240)
                (args.output/f'font-{index}-{fi}-probe.log').write_text(probe.stdout+probe.stderr,encoding='utf-8')
                stats=re.search(r'font_probe total=(\d+) missing=(\d+) gray=(\d+) black=(\d+) status=(\d+) peak=(\d+) used=(\d+) live=(\d+)',probe.stdout)
                # 探针以1报告缺字；仅在统计完整且无引擎错误时接受这个诊断退出码。
                # The probe reports missing glyphs with exit 1; accept it only with complete statistics and no engine errors.
                valid=bool(stats and stats[5]=='0' and stats[7]=='0' and stats[8]=='0' and stats[3]=='1' and stats[4]=='1' and probe.returncode==int(int(stats[2])>0))
                result={'book':book.name,'font':name,'probe_ok':valid,'metadata':next((line for line in probe.stdout.splitlines() if line.startswith('font_probe_info ')),'')}
                if stats:result.update({'rendered_characters_with_sizes':int(stats[1]),'missing_unique_body_characters':int(stats[2])//5,'font_pool_peak_bytes':int(stats[6]),'font_pool_budget_bytes':1024*1024})
                report['fonts'].append(result);failed|=not valid
                script='library,enter,toc,back,jump,'+'library-next,'*5+'enter,next,previous,back,enter,back,quit'
                command=[str(args.sim.resolve()),'--library',str(library),'--state-dir',str(root/f'state-{index}-{fi}')]
                if font:command+=['--font',str(runtime_font)]
                session=subprocess.run(command,env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT=script),capture_output=True,text=True,timeout=180)
                (args.output/f'book-{index}-{fi}-session.log').write_text(session.stdout+session.stderr,encoding='utf-8')
                positions=re.findall(r'epub_library_position ([^\n]+)',session.stdout)
                opened=session.stdout.count(f'library_open name={book.name}')==2
                restored=len(positions)==2 and positions[0]!=positions[1]
                # 跳转后保存的位置必须有非零锚点，而不是重新落回初始封面。
                # The saved post-jump location needs a nonzero anchor instead of returning to the initial cover.
                ok=session.returncode==0 and opened and restored and session.stdout.count('library_return status=0')==2 and session.stdout.count('library_turn pointer=0 status=0')==2 and 'used=0 live=0' in session.stdout
                result['reading_restore_ok']=ok
                if not font:oracle['built_in_reading_restore_ok']=ok
                failed|=not ok
                print(f'{"PASS" if ok and valid else "FAIL"} reading/font {name}'+(f' (missing body characters: {result.get("missing_unique_body_characters","unknown")})' if font else ''),flush=True)
        unchanged=all(digest(p)==sha for _,p,sha in sources)
        report['originals_unchanged']=unchanged;failed|=not unchanged
    report['passed']=not failed
    (args.output/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return int(failed)

if __name__=='__main__':
    raise SystemExit(main())
