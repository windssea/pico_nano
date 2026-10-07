"""目录层级/章节/片段及失败原子性。 / Navigation hierarchy, chapters, fragments and failure atomicity."""
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import zipfile
import struct

binary = sys.argv[1]
container = '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>'
ncx = '''<!DOCTYPE ncx SYSTEM "http://example.invalid/ncx.dtd"><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/"><navMap>
<navPoint id="first"><navLabel><text> 卷一 &amp; Book </text></navLabel><content src="Text/a.xhtml#%E7%AB%A0%E4%B8%80"/>
<navPoint id="second"><navLabel><text>第二章</text></navLabel><content src="Text/b.xhtml"/></navPoint></navPoint></navMap></ncx>'''
nav = '''<h:html xmlns:h="http://www.w3.org/1999/xhtml" xmlns:e="http://www.idpf.org/2007/ops"><h:body>
<h:nav e:type="landmarks"><h:ol><h:li><h:a href="bad.xhtml">ignore</h:a></h:li></h:ol></h:nav>
<h:nav e:type="toc"><h:h1>目录标题不是条目</h:h1><h:ol><h:li><h:span>卷一</h:span><h:ol>
<h:li><h:a href="Text/a.xhtml#%E7%AB%A0%E4%B8%80"> 第一 <h:strong>章</h:strong> </h:a></h:li>
<h:li><h:a href="Text/b.xhtml"> 第二章 </h:a></h:li></h:ol></h:li></h:ol></h:nav></h:body></h:html>'''
with tempfile.TemporaryDirectory() as folder:
    path = pathlib.Path(folder) / "book.epub"
    def write(content=ncx, epub3=False, repeat=False):
        package = f'''<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="{'3.0' if epub3 else '2.0'}" unique-identifier="uid">
<metadata><d:title>目录测试</d:title><d:identifier id="uid">test</d:identifier></metadata><manifest>
<item id="b" href="Text/b.xhtml" media-type="application/xhtml+xml"/><item id="a" href="Text/a.xhtml" media-type="application/xhtml+xml"/>
<item id="toc" href="{'nav.xhtml' if epub3 else 'toc.ncx'}" media-type="{'application/xhtml+xml' if epub3 else 'application/x-dtbncx+xml'}" {'properties="nav"' if epub3 else ''}/>
</manifest><spine {'toc="toc"' if not epub3 else ''}><itemref idref="a"/><itemref idref="b" linear="no"/></spine></package>'''
        if repeat:
            package=package.replace('</spine>', '<itemref idref="a"/></spine>')
        with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as book:
            book.writestr("mimetype", b"application/epub+zip", compress_type=zipfile.ZIP_STORED)
            book.writestr("META-INF/container.xml", container)
            book.writestr("OPS/book.opf", package)
            book.writestr("OPS/Text/a.xhtml", b"body")
            book.writestr("OPS/Text/b.xhtml", b"body")
            book.writestr("OPS/nav.xhtml" if epub3 else "OPS/toc.ncx", content, compress_type=zipfile.ZIP_STORED)
    def run(success=True, env=None):
        result = subprocess.run([binary, str(path), "--toc"], capture_output=True, timeout=30, env=dict(os.environ, **(env or {})))
        assert (result.returncode==0)==success, result.stderr
        assert b"used=0 live=0" in result.stderr, result.stderr
        if not success:
            assert not result.stdout, "Failed navigation must not publish partial JSON"
        return result
    write();result=run();entries=json.loads(result.stdout)["toc"]
    assert [e["label"] for e in entries]==["卷一 & Book","第二章"]
    assert [e["level"] for e in entries]==[0,1]
    assert [e["spine_index"] for e in entries]==[0,1] and entries[0]["fragment"]=="章一"
    attempts=int(re.search(rb"attempts=(\d+)",result.stderr)[1])
    for fault in range(1,attempts+1):run(False,{"PN_EPUB_FAIL_AT":str(fault)})
    write(repeat=True);assert json.loads(run().stdout)["toc"][0]["spine_index"]==0
    write(ncx.encode('utf-16'));run()
    write();result=subprocess.run([binary,str(path),"--toc"],capture_output=True,env=dict(os.environ,PN_EPUB_DETACH="1"),timeout=30)
    assert result.returncode!=0 and b"status=6" in result.stderr and b"used=0 live=0" in result.stderr
    with zipfile.ZipFile(path) as book:
        offset=book.getinfo('OPS/toc.ncx').header_offset
    raw=bytearray(path.read_bytes());names,extras=struct.unpack_from('<HH',raw,offset+26)
    begin=offset+30+names+extras;at=raw.index(b'id="first"',begin);raw[at+4]=ord('z');path.write_bytes(raw);run(False)
    write(nav,True);entries=json.loads(run().stdout)["toc"]
    assert [e["label"] for e in entries]==["卷一","第一 章","第二章"]
    assert [e["level"] for e in entries]==[0,1,1]
    assert not entries[0]["target"] and entries[0]["spine_index"] is None
    assert entries[1]["fragment"]=="章一" and entries[2]["spine_index"]==1
    for content,epub3 in [(ncx.replace("Text/b.xhtml","missing.xhtml"),False),
                         (ncx.replace("Text/b.xhtml","https://example.invalid/book"),False),
                         (ncx.replace("Text/b.xhtml","../../escape.xhtml"),False),
                         (ncx.replace("第二章",""),False),
                         (ncx.replace("第二章","字"*200),False),
                         (ncx.replace('http://www.daisy.org/z3986/2005/ncx/','urn:wrong'),False),
                         (ncx.replace('</navMap>', '</navMap><navMap/>'),False),
                         (nav.replace('e:type="toc"','e:type="landmarks"'),True),
                         (nav.replace('卷一</h:span>','</h:span>'),True),
                         (nav.replace('Text/b.xhtml','nav.xhtml'),True),
                         (nav.replace('Text/b.xhtml','Text/b.xhtml?query'),True)]:
        write(content,epub3);run(False)
    print("toc: NCX/nav hierarchy, normalized labels, groups, fragments and allocation faults passed")
