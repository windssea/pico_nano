"""失败预算缓存不重复解码，预算增长可恢复。 / Failure-budget caching avoids repeated decoding and recovers on budget growth."""
import pathlib,zipfile,tempfile,subprocess,os,sys,re
with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder);books=root/'books';books.mkdir();cache=root/'covers';book=books/'large.epub'
    with zipfile.ZipFile(book,'w',compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
        z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('book.opf','<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="3.0" unique-identifier="u"><metadata><d:title>Original budget fixture</d:title><d:identifier id="u">cover-budget</d:identifier></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/><item id="c" href="cover.jpg" media-type="image/jpeg" properties="cover-image"/></manifest><spine><itemref idref="a"/></spine></package>')
        z.writestr('a.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><body><p>Original fixture</p></body></html>')
        z.writestr('cover.jpg',(pathlib.Path(sys.argv[2])/'jpeg-large-progressive.jpg').read_bytes())
    def run(budget):
        result=subprocess.run([sys.argv[1],'queue',str(books),str(root/'out.pgm'),str(cache)],env=dict(os.environ,PN_COVER_QUEUE_BUDGET=str(budget)),capture_output=True,text=True,timeout=30)
        assert result.returncode==0,(result.stdout,result.stderr)
        return result.stdout,int(re.search(r'decode peak=(\d+)',result.stdout)[1])
    cold,cold_peak=run(100000);assert 'reason=5 ' in cold,cold
    warm,warm_peak=run(100000);assert 'reason=5 ' in warm and warm_peak<cold_peak,(cold,warm)
    recovered,_=run(6*1024*1024);assert 'state=2 reason=0 ' in recovered,recovered
print('cover failure budgets: same-budget cache, smaller warm peak and larger-budget recovery passed')
