"""真实产品循环的接管/取消/锁屏恢复；文件和硬件port均隔离。/ Isolated real-product-loop takeover/cancel/lock recovery with file and hardware ports."""
import pathlib
import subprocess
import sys
import tempfile
import zipfile
import shutil

with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path('/sdcard/piconano/books');root.mkdir(parents=True,exist_ok=True)
    state=pathlib.Path('/data/progress');state.mkdir(parents=True,exist_ok=True)
    font=pathlib.Path('/sdcard/piconano/fonts/recovery.ttf');font.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(pathlib.Path(__file__).resolve().parents[1]/'fixtures/font-fallback.ttf',font)
    book=root/'loop.epub'
    with zipfile.ZipFile(book,'w') as z:
        z.writestr('mimetype','application/epub+zip')
        z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('OPS/book.opf','<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="2.0" unique-identifier="id"><metadata><d:identifier id="id">loop</d:identifier><d:title>Loop</d:title></metadata><manifest><item id="body" href="body.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="body"/></spine></package>')
        z.writestr('OPS/body.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><body>'+('<p>product loop takeover and lock recovery 0123456789</p>'*2000)+'</body></html>')
    try:
        for case in ('success','cancel','lock','retry','font','turn','rapid'):
            result=subprocess.run([sys.argv[1],str(book),case],capture_output=True,text=True,timeout=60)
            assert result.returncode==0,(case,result.stdout,result.stderr)
            print(result.stdout.strip(),flush=True)
    finally:
        book.unlink(missing_ok=True)
        font.unlink(missing_ok=True)
        shutil.rmtree(state)
