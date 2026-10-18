"""补字启动的同句柄校验、软回退和取消/失卡/显示失败释放。 / Same-handle fallback verification, soft recovery and cancel/media/display cleanup."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile


with tempfile.TemporaryDirectory() as folder:
    root=Path(folder)
    text=root/'book.txt';text.write_text(('中文 fallback example 0123456789。\n'*100),encoding='utf8')
    epub=root/'book.epub'
    with zipfile.ZipFile(epub,'w') as z:
        z.writestr('mimetype','application/epub+zip')
        z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('OPS/book.opf','<package xmlns="http://www.idpf.org/2007/opf" xmlns:dc="http://purl.org/dc/elements/1.1/" version="2.0" unique-identifier="id"><metadata><dc:identifier id="id">fallback-startup</dc:identifier><dc:title>Fallback startup</dc:title></metadata><manifest><item id="body" href="body.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="body"/></spine></package>')
        z.writestr('OPS/body.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Fallback startup</title></head><body>'+('<p>中文 fallback example 0123456789。</p>'*100)+'</body></html>')
    for book in (text,epub):
        for fault in ('normal','missing','replaced','bad-primary','cancel','lost','present'):
            case=root/(book.suffix[1:]+'-'+fault);case.mkdir();state=case/'state';state.mkdir();font=case/'font.ttf';shutil.copy2(sys.argv[2],font)
            env=dict(os.environ);env.pop('PN_FONT_STARTUP_FAULT',None)
            if fault!='normal':env['PN_FONT_STARTUP_FAULT']=fault
            result=subprocess.run([sys.argv[1],str(book),str(font),str(state)],env=env,capture_output=True,text=True,timeout=30)
            assert result.returncode==0,(book.suffix,fault,result.stdout,result.stderr)
            row=next(line for line in result.stdout.splitlines() if line.startswith('startup '));fields={k:int(v) for k,v in (word.split('=') for word in row.split()[1:])}
            assert fields['used']==fields['live']==0
            if fault=='normal':assert fields['full_hashes']==fields['book_hashes']==fields['font_opens']==fields['book_opens']==1 and fields['font_get_hashes']==0 and fields['book_read_bytes']>=book.stat().st_size and fields['book_reads']>0,fields
            if fault in ('normal','missing','replaced','bad-primary'):assert fields['status']==0 and fields['presents']>=1
            print(json.dumps({'format':book.suffix,'fault':fault,**fields}),flush=True)
    # 冷开外置主字体：首屏前不得整读字体；首屏后后台复核才支付完整SHA。/ Cold external primary: no whole-font read before the first frame; the background check pays it afterwards.
    case=root/'epub-primary-deferred';case.mkdir();state=case/'state';state.mkdir();font=case/'font.ttf';shutil.copy2(sys.argv[2],font)
    env=dict(os.environ,PN_FONT_STARTUP_PRIMARY='1',PN_FONT_STARTUP_VERIFY='1')
    result=subprocess.run([sys.argv[1],str(epub),str(font),str(state)],env=env,capture_output=True,text=True,timeout=30)
    assert result.returncode==0,(result.stdout,result.stderr)
    row=next(line for line in result.stdout.splitlines() if line.startswith('startup '));fields={k:int(v) for k,v in (word.split('=') for word in row.split()[1:])}
    assert fields['status']==0 and fields['presents']>=1 and fields['full_hashes']==0 and fields['deferred_hashes']==1 and fields['font_opens']==1,fields
    print(json.dumps({'format':'epub-primary-deferred',**fields}),flush=True)
