"""跨进程启动包完整身份、像素与原解析退路。 / Cross-process bootstrap identity, exact pixels and parser fallback."""
import hashlib
import json
import os
import pathlib
import shutil
import sys
import tempfile
import zlib
import zipfile
from test_resume import fixture, run
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[2]/'tools'))
from test_local_book_index import fixture as large_fixture

binary,font=sys.argv[1:3]
with tempfile.TemporaryDirectory() as tmp:
    root=pathlib.Path(tmp)
    fixture(root)
    for label in ('small','multi','single','image'):
        book=root/'book.epub'
        if label=='multi':large_fixture(book,48,12000)
        if label=='single':large_fixture(book,1,576000)
        if label=='image':
            fixture(root)
            image=root/'image.epub'
            with zipfile.ZipFile(book) as src,zipfile.ZipFile(image,'w',compression=zipfile.ZIP_DEFLATED) as dst:
                for name in src.namelist():
                    content=src.read(name)
                    if name=='a.xhtml':content=content.replace(b'<body>',b'<body><img src="picture.png"/>',1)
                    if name=='book.opf':content=content.replace(b'</manifest>',b'<item id="im" href="picture.png" media-type="image/png"/></manifest>')
                    dst.writestr(name,content,compress_type=zipfile.ZIP_STORED if name=='mimetype' else zipfile.ZIP_DEFLATED)
                dst.writestr('picture.png',(pathlib.Path(__file__).resolve().parents[1]/'fixtures/image-black.png').read_bytes())
            book=image
        seed=root/(label+'-seed')
        seeded,_=run(binary,'epub',book,font,seed,root/(label+'-seed-frames'),'seed-first' if label=='image' else 'seed',dict(os.environ,PN_BOOTSTRAP_SEED='1'))
        assert seeded['boot_stored']==1 and seeded['boot_hits']==0,seeded
        packet=next((seed/'reading/resume').glob('*.pnbs'))
        trusted=next((seed/'resume').glob('*.pnbt'))
        payload=packet.read_bytes();proof=trusted.read_bytes()
        assert proof[:4]==b'PNBR' and proof[8:40]==hashlib.sha256(payload).digest()
        assert int.from_bytes(proof[72:],'little')==zlib.crc32(proof[:72])
        reference=root/(label+'-reference');shutil.copytree(seed,reference)
        for p in reference.rglob('*.pnbs*'):p.unlink()
        for p in (reference/'reading/resume').glob('*.pnr*'):p.unlink()
        expected_result,expected=run(binary,'epub',book,font,reference,root/(label+'-reference-frames'))
        assert expected_result['boot_hits']==0
        for case in ('warm','missing','root-missing','corrupt','truncated','version','root-corrupt','backup'):
            state=root/(label+'-'+case);shutil.copytree(seed,state)
            p=state/packet.relative_to(seed);t=state/trusted.relative_to(seed)
            if case=='missing':p.unlink()
            elif case=='root-missing':t.unlink()
            elif case=='root-corrupt':t.write_bytes(proof[:-1]+bytes([proof[-1]^1]))
            elif case in ('corrupt','truncated','version'):
                content=bytearray(payload)
                if case=='truncated':del content[len(content)//2:]
                elif case=='corrupt':content[len(content)//2]^=1
                else:
                    content[48]^=1
                    content[60:64]=zlib.crc32(content[:60]).to_bytes(4,'little')
                p.write_bytes(content)
            elif case=='backup':p.rename(p.with_suffix(p.suffix+'.bak'));t.rename(t.with_suffix(t.suffix+'.bak'))
            before={str(f.relative_to(state)):hashlib.sha256(f.read_bytes()).hexdigest() for f in state.rglob('*.pnb*') if f.is_file()}
            result,actual=run(binary,'epub',book,font,state,root/(label+'-'+case+'-frames'))
            assert actual==expected,(label,case,result)
            assert result['boot_hits']==(1 if case in ('warm','backup') else 0),(label,case,result)
            after={str(f.relative_to(state)):hashlib.sha256(f.read_bytes()).hexdigest() for f in state.rglob('*.pnb*') if f.is_file()}
            assert before==after,(label,case,'fallback modified cache')
            print(label,case,json.dumps(result),flush=True)
        if label=='small':
            original=book.read_bytes();timestamp=book.stat();changed=bytearray(original)
            changed[10]^=2;central=changed.index(b'PK\x01\x02');changed[central+12]^=2
            book.write_bytes(changed);os.utime(book,ns=(timestamp.st_atime_ns,timestamp.st_mtime_ns))
            changed_state=root/'same-stat';shutil.copytree(seed,changed_state)
            result,actual=run(binary,'epub',book,font,changed_state,root/'same-stat-frames')
            _,fresh=run(binary,'epub',book,font,root/'fresh-source',root/'fresh-source-frames')
            assert result['boot_hits']==0 and actual==fresh and book.stat().st_size==timestamp.st_size
            book.write_bytes(original);os.utime(book,ns=(timestamp.st_atime_ns,timestamp.st_mtime_ns))
