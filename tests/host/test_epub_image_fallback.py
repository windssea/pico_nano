"""可选图片坏/缺与启动字体缺失不阻断正文。 / Optional bad/missing images and absent startup faces do not block text."""
import pathlib,subprocess,sys,tempfile,zipfile
with tempfile.TemporaryDirectory() as tmp:
    root=pathlib.Path(tmp);book=root/'book.epub'
    with zipfile.ZipFile(book,'w',compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
        z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('OPS/book.opf','<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="2.0" unique-identifier="uid"><metadata><d:title>容错</d:title><d:identifier id="uid">fallback</d:identifier></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/><item id="img" href="bad.png" media-type="image/png"/></manifest><spine><itemref idref="a"/></spine></package>')
        z.writestr('OPS/a.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><body><img src="bad.png"/>'+''.join('<p>图片不能显示时仍可继续阅读正文。 X 曈。</p>' for _ in range(100))+'</body></html>')
        z.writestr('OPS/bad.png',b'not an image')
    for page in (1,2):
        image=root/f'page-{page}.pgm'
        run=subprocess.run([sys.argv[1],str(book),'/missing-optional.ttf',str(page),str(image),'44'],capture_output=True,text=True,timeout=30)
        assert run.returncode==0,(run.stdout,run.stderr)
        assert image.read_bytes().startswith(b'P5\n684 1216\n255\n')
        assert 'used=0 live=0' in run.stdout+run.stderr
