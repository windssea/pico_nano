"""原创有样式/图片与长段缓存语料。 / Original styled/image and long-paragraph cache fixtures."""
import pathlib,subprocess,sys,tempfile,zipfile,struct,zlib
def create(path,large=False,marker='Original'):
    with zipfile.ZipFile(path,'w',compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
        z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('book.opf','<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="3.0" unique-identifier="u"><metadata><d:title>Original persistent semantics</d:title><d:identifier id="u">pn-semantic-disk</d:identifier></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/><item id="c" href="a.css" media-type="text/css"/><item id="p" href="pic.png" media-type="image/png"/></manifest><spine><itemref idref="a"/></spine></package>')
        if large:body='<html xmlns="http://www.w3.org/1999/xhtml"><body><p>'+'ABCDEFGHIJKLMNOPQRSTUVWXYZ 阅读缓存的原创样本。 '*24000+'</p></body></html>'
        else:body='<html xmlns="http://www.w3.org/1999/xhtml"><head><link rel="stylesheet" href="a.css"/></head><body><h2>'+marker+' title</h2>'+''.join('<p>第'+str(i)+'段 Original styled text、阅读与缓存。 '*20+'</p>' for i in range(80))+'<img src="pic.png" alt="original"/></body></html>'
        z.writestr('a.xhtml',body);z.writestr('a.css','p {font-style:italic} h2 {text-align:center} img {width:60%;max-height:90px}')
        def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
        z.writestr('pic.png',b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',1,1,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(b'\x00\x22\x66\xaa'))+chunk(b'IEND',b''))
    return path
if __name__=='__main__':
    with tempfile.TemporaryDirectory() as folder:
        root=pathlib.Path(folder);small=create(root/'small.epub');large=create(root/'large.epub',True);changed=create(root/'changed.epub',marker='Changed')
        subprocess.run([sys.argv[1],str(small),str(large),sys.argv[2],str(root),str(changed)],check=True,timeout=180)
