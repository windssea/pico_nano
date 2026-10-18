"""原创常规/后置样式/损坏尾部与CRC语料。 / Original regular, late-style, bad-tail and CRC fixtures."""
import pathlib,struct,subprocess,sys,tempfile,zipfile
with tempfile.TemporaryDirectory() as folder:
    book=pathlib.Path(folder)/'passes.epub'
    opening='<html xmlns="http://www.w3.org/1999/xhtml">'
    head='<head><link rel="stylesheet" href="base.css"/><style>p.x {font-style:italic;text-align:center}</style></head>'
    words='中文 Word   &#x1f600; &amp; 内容 ' * 150
    body='<body><p class="x" id="start">'+words+'<b>粗体</b> 尾部</p><pre> 空格\n换行 </pre><img src="image.png" alt="替代"/></body>'
    big='<body><p class="x" id="large">'+words*35+'</p></body>'
    cases={'normal.xhtml':opening+head+body+'</html>','large.xhtml':opening+head+big+'</html>',
           'late.xhtml':opening+'<body><p class="hide">'+words+'</p><img src="http://invalid.example/a.png"/><p id="keep">'+words+'</p></body><head><style>.hide,img {display:none} p {font-weight:bold}</style></head></html>',
           'multi.xhtml':opening+head+'<head><style>p.x {text-align:left!important}</style></head>'+body+'</html>',
           'bare.xhtml':opening+body+'</html>',
           'compatible.xhtml':opening+head+('<body><p>'+('A & B literal <Cardinal> text '*2000)+'</p></body></html>'),
           'bad.xhtml':opening+head+big+'</wrong>',
           'crc.xhtml':opening+head+big+'</html>',
           'hidden.xhtml':opening+'<body><p class="hide" id="secret">'+words+'</p></body><head><style>.hide {display:none}</style></head></html>',
           'duplicate.xhtml':opening+head+'<body><p id="dup">'+words+'</p><p id="dup">重复</p></body></html>'}
    with zipfile.ZipFile(book,'w',compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
        z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        names=list(cases)+['base.css','image.png']
        kinds=['application/xhtml+xml']*len(cases)+['text/css','image/png']
        z.writestr('book.opf','<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="2.0" unique-identifier="u"><metadata><d:title>Original pass comparison</d:title><d:identifier id="u">original-passes</d:identifier></metadata><manifest>'+''.join(f'<item id="i{i}" href="{n}" media-type="{kinds[i]}"/>' for i,n in enumerate(names))+'</manifest><spine>'+''.join(f'<itemref idref="i{i}"/>' for i in range(len(cases)))+'</spine></package>')
        for name,text in cases.items():z.writestr(name,text)
        z.writestr('base.css','p {font-weight:bold} p.x {text-align:right} img {width:60%;max-height:80px}')
        z.writestr('image.png',b'Original identity fixture, not decoded in this test')
    # 中央与本地头一致但正文CRC错误，容器结构校验后仍须在实际EOF拒绝。/ Matching local/central headers with a wrong body CRC must still fail at actual resource EOF.
    with zipfile.ZipFile(book) as z:info=z.getinfo('crc.xhtml');start=z.start_dir
    data=bytearray(book.read_bytes());wrong=info.CRC^1;struct.pack_into('<I',data,info.header_offset+14,wrong)
    cursor=start
    while data[cursor:cursor+4]==b'PK\x01\x02':
        n,e,c=struct.unpack_from('<HHH',data,cursor+28)
        if data[cursor+46:cursor+46+n]==b'crc.xhtml':struct.pack_into('<I',data,cursor+16,wrong);break
        cursor+=46+n+e+c
    else:raise AssertionError('missing CRC fixture entry')
    book.write_bytes(data)
    subprocess.run([sys.argv[1],str(book)],check=True,timeout=120)
