"""构造EPUB验证同源出版物结构及拒绝路径。 / Construct EPUBs to verify shared publication structure and rejection paths."""
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
container = '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>'
package = '''<o:package xmlns:o="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="2.0" unique-identifier="uid">
<o:metadata><d:title>测试 &amp; Book</d:title><d:creator>作者</d:creator><d:identifier id="uid">urn:test:1</d:identifier><d:language>zh</d:language><o:meta name="cover" content="cover"/></o:metadata>
<o:manifest><o:item id="two" href="Text/b.xhtml" media-type="application/xhtml+xml"/><o:item id="one" href="Text/a.xhtml" media-type="application/xhtml+xml"/><o:item id="cover" href="Images/cover.jpg" media-type="image/jpeg"/><o:item id="toc" href="toc.ncx" media-type="application/x-dtbncx+xml"/></o:manifest>
<o:spine toc="toc"><o:itemref idref="one"/><o:itemref idref="two" linear="no"/></o:spine></o:package>'''
with tempfile.TemporaryDirectory() as folder:
    path = pathlib.Path(folder) / "book.epub"
    def write(opf=package, con=container, mimetype=b"application/epub+zip", extra=None, stored_opf=False):
        with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as book:
            book.writestr("mimetype", mimetype, compress_type=zipfile.ZIP_STORED)
            book.writestr("META-INF/container.xml", con)
            book.writestr("OPS/book.opf", opf, compress_type=zipfile.ZIP_STORED if stored_opf else zipfile.ZIP_DEFLATED)
            for name in ["Text/a.xhtml", "Text/b.xhtml", "Images/cover.jpg", "toc.ncx"]:
                book.writestr("OPS/" + name, b"resource")
            if extra:
                for name, content in extra.items():
                    book.writestr(name, content)
    def run(success=True, env=None):
        result = subprocess.run([binary, str(path)], capture_output=True, timeout=30, env=dict(os.environ, **(env or {})))
        assert (result.returncode == 0) == success, result.stderr
        assert b"used=0 live=0" in result.stderr, result.stderr
        return result
    write()
    result = run()
    data = json.loads(result.stdout)
    assert data["title"] == "测试 & Book" and data["creator"] == "作者"
    assert data["spine_count"] == 2 and data["manifest_count"] == 4
    assert [x["id"] for x in data["spine"]] == ["one", "two"]
    assert [x["linear"] for x in data["spine"]] == [True, False]
    assert data["cover_declared"] and data["cover_path"] == "OPS/Images/cover.jpg"
    attempts = int(re.search(rb"attempts=(\d+)", result.stderr)[1])
    for fault in range(1, attempts + 1):
        run(False, {"PN_EPUB_FAIL_AT": str(fault)})
    run(False, {"PN_EPUB_DETACH": "1"})
    write(opf=package.encode("utf-16"), con=container.encode("utf-16"));run()
    write(opf="<!DOCTYPE o:package SYSTEM 'http://example.invalid/opf.dtd'>" + package);run()
    epub3 = package.replace('version="2.0"', 'version="3.0"').replace('id="one" href=', 'id="one" properties="nav" href=').replace('<o:meta name="cover" content="cover"/>', '<o:meta property="rendition:layout">pre-paginated</o:meta>').replace('id="cover" href=', 'id="cover" properties="cover-image" href=')
    write(opf=epub3);data=json.loads(run().stdout)
    assert data["version"]==3 and data["nav_path"]=="OPS/Text/a.xhtml" and data["fixed_layout"] and data["cover_declared"]
    per_item = package.replace('idref="two" linear="no"', 'idref="two" linear="no" properties="rendition:layout-pre-paginated"')
    write(opf=per_item);assert json.loads(run().stdout)["fixed_layout"]
    items = ''.join(f'<o:item id="c{i}" href="Text/c{i}.xhtml" media-type="application/xhtml+xml"/>' for i in range(1000))
    refs = ''.join(f'<o:itemref idref="c{i}"/>' for i in reversed(range(1000)))
    large = re.sub(r'<o:manifest>.*?</o:manifest>', '<o:manifest>'+items+'</o:manifest>', package, flags=re.S)
    large = re.sub(r'<o:spine.*?</o:spine>', '<o:spine>'+refs+'</o:spine>', large, flags=re.S)
    large = large.replace('<o:meta name="cover" content="cover"/>', '')
    write(opf=large, extra={f'OPS/Text/c{i}.xhtml':b'resource' for i in range(1000)})
    data=json.loads(run().stdout)
    assert data["spine_count"]==1000 and [x["id"] for x in data["spine"]]==[f'c{i}' for i in reversed(range(1000))]
    write(stored_opf=True)
    with zipfile.ZipFile(path) as book:
        info = book.getinfo("OPS/book.opf")
    raw = bytearray(path.read_bytes())
    names, extras = struct.unpack_from("<HH", raw, info.header_offset + 26)
    begin = info.header_offset + 30 + names + extras
    at = raw.index(b"urn:test:1", begin)
    raw[at + 9] = ord("2")
    path.write_bytes(raw);result=run(False)
    assert not result.stdout, "A CRC failure must not publish provisional metadata"
    for invalid in [package.replace('idref="one"', 'idref="absent"'),
                    package.replace('id="two"', 'id="one"'),
                    package.replace('Text/a.xhtml', '../../escape.xhtml'),
                    package.replace('Text/a.xhtml', 'https://example.invalid/book'),
                    package.replace('application/xhtml+xml', 'application/pdf'),
                    package.replace('urn:test:1', ''),
                    package.replace('http://www.idpf.org/2007/opf', 'urn:wrong'),
                    package.replace('version="2.0"', 'version="9.0"'),
                    "<!DOCTYPE o:package [<!ENTITY custom 'bad'>]>" + package]:
        write(opf=invalid);run(False)
    write(mimetype=b"application/zip");run(False)
    write(extra={"META-INF/encryption.xml": b"encrypted"});run(False)
    write(opf=package.replace('version="2.0"', 'version="2.0" xml:base="elsewhere/"'));run(False)
    print("epub: metadata, namespaces, UTF16, spine identities and all allocation faults passed")
