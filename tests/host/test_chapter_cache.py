"""原创章节缓存及损坏CRC样本。 / Original chapter-cache and corrupt-CRC fixtures."""
import pathlib,zipfile,tempfile,subprocess,struct,sys
with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder);book=root/'cache.epub'
    with zipfile.ZipFile(book,'w',compression=zipfile.ZIP_STORED) as archive:
        archive.writestr('mimetype','application/epub+zip')
        archive.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        archive.writestr('book.opf','<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="2.0" unique-identifier="u"><metadata><d:title>Original cache test</d:title><d:identifier id="u">cache</d:identifier></metadata><manifest>'+''.join(f'<item id="{i}" href="{name}.xhtml" media-type="application/xhtml+xml"/>' for i,name in enumerate(('a','b','large')) )+'</manifest><spine><itemref idref="0"/><itemref idref="1"/><itemref idref="2"/></spine></package>')
        for name,size in [('a',6000),('b',8000),('large',140000)]:archive.writestr(name+'.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><body><p>'+name*size+'</p></body></html>')
    subprocess.run([sys.argv[1],str(book),'normal'],check=True,timeout=30)
    with zipfile.ZipFile(book) as archive:offset=archive.getinfo('a.xhtml').header_offset
    raw=bytearray(book.read_bytes());name,extra=struct.unpack_from('<HH',raw,offset+26);raw[offset+30+name+extra+80]^=1
    broken=root/'broken.epub';broken.write_bytes(raw)
    subprocess.run([sys.argv[1],str(broken),'corrupt'],check=True,timeout=30)
print('chapter cache: borrowers, independent cursors, low-memory/large fallback, stale media and CRC passed')
