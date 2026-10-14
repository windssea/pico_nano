"""下一页出现全新字形，避免用第一屏已热的字符制造速度结果。 / New glyphs on the next page avoid confusing first-page cache heat with warming."""
import html,pathlib,subprocess,sys,tempfile,zipfile
with tempfile.TemporaryDirectory() as folder:
    book=pathlib.Path(folder)/'warm.txt';text='A'*450+'\n'+''.join(chr(33+i%90) for i in range(5000));book.write_text(text,encoding='utf-8')
    epub=pathlib.Path(folder)/'warm.epub'
    with zipfile.ZipFile(epub,'w',compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
        z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('book.opf','<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="3.0" unique-identifier="u"><metadata><d:title>Original warming</d:title><d:identifier id="u">pn-warming</d:identifier></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="a"/></spine></package>')
        z.writestr('a.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Original</title></head><body><p>'+html.escape(text)+'</p></body></html>')
    subprocess.run([sys.argv[1],str(book),str(epub)],check=True,timeout=90)
