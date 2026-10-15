"""原创EPUB/外置渐进封面的可恢复任务。 / Resumable jobs for original EPUB and external progressive covers."""
import pathlib,struct,subprocess,sys,tempfile,zipfile,zlib
with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder);book=root/'original.epub';text=root/'external.txt';text.write_text('Original source',encoding='utf8')
    (root/'external.cover.jpg').write_bytes(pathlib.Path(sys.argv[2]).read_bytes())
    def chunk(k,b):return struct.pack('>I',len(b))+k+b+struct.pack('>I',zlib.crc32(k+b))
    raw=b''.join(b'\0'+bytes((x*13+y*7)%256 for x in range(173)) for y in range(391))
    png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',173,391,8,0,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')
    with zipfile.ZipFile(book,'w',compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
        z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('book.opf','<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="3.0" unique-identifier="u"><metadata><d:title>Original job</d:title><d:identifier id="u">job</d:identifier></metadata><manifest><item id="p" href="a.xhtml" media-type="application/xhtml+xml"/><item id="c" href="cover.png" media-type="image/png" properties="cover-image"/></manifest><spine><itemref idref="p"/></spine></package>')
        z.writestr('a.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><body><p>Original</p></body></html>');z.writestr('cover.png',png)
    subprocess.run([sys.argv[1],str(book),str(text)],check=True,timeout=90)
