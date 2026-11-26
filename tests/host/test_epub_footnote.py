"""EPUB 脚注解析、命中、弹窗与跳转端到端测试夹具。 / End-to-end EPUB footnote test fixture."""
import pathlib, subprocess, sys, tempfile, zipfile

with tempfile.TemporaryDirectory() as folder:
    root = pathlib.Path(folder)
    book = root / "footnotes.epub"
    state = root / "state"
    icon_png = (pathlib.Path(sys.argv[2]).parents[2] / "tests" / "fixtures" / "image-black.png").read_bytes()
    with zipfile.ZipFile(book, "w", compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        z.writestr(
            "META-INF/container.xml",
            '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container">'
            '<rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles>'
            "</container>",
        )
        z.writestr(
            "OPS/book.opf",
            '<package xmlns="http://www.idpf.org/2007/opf" xmlns:dc="http://purl.org/dc/elements/1.1/" version="3.0" unique-identifier="uid">'
            "<metadata><dc:title>西游注本测试</dc:title><dc:identifier id=\"uid\">fn-test</dc:identifier></metadata>"
            "<manifest>"
            '<item id="ch1" href="ch1.xhtml" media-type="application/xhtml+xml"/>'
            '<item id="notes" href="notes.xhtml" media-type="application/xhtml+xml"/>'
            '<item id="icon" href="icon.png" media-type="image/png"/>'
            "</manifest>"
            '<spine><itemref idref="ch1"/><itemref idref="notes"/></spine>'
            "</package>",
        )
        z.writestr("OPS/icon.png", icon_png)
        long_alt = "超长屏幕阅读器替代说明，用于模拟微信读书与商业出版物在脚注小图标上附带数百字节完整译者注的场景。" * 4
        long_note = (
            "水精盘中之珠。(1)混沌：古时传说，世界没有开辟时，天地不分，模糊一片。"
            "《河图·括地象》说：易有太极，是生两仪，两仪未分，其气混沌；清浊既分，伏者为天，偃者为地。"
            "这一段特意写得很长，用来验证脚注弹窗在六行正文容量下能够正确分页，并在多页之间顺畅翻页阅读。"
            "继续补充更多笺注说明：古人常以鸿蒙混沌喻未开之境，悟元子评注认为须破顽空方能见真性，"
            "因此本回末尾特以诗句点题，并附此长注供读者在阅读正文时随点随看，亦可点击左下角前往脚注全文位置查看完整上下文。"
        )
        # 构造第 1 页：顶部有 [103]（触发居底弹窗）、中间有隐藏 aside [注2] 与跨文件 [3]、底部区域有 [4]（触发居顶弹窗），并混入一条全行目录链接验证不拦截。
        filler = "".join(
            f"<p>正文第{i}段，鸿蒙初辟原无姓，打破顽空须悟空，人事天机皆在此中。</p>"
            for i in range(1, 6)
        )
        ch1 = (
            '<?xml version="1.0" encoding="utf-8"?>'
            '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">'
            "<head><title>第一回</title></head>"
            "<body>"
            '<p id="ref1">鸿蒙初辟原无姓，打破顽空须悟空<sup><a href="#fn1">[103]</a></sup>。'
            '又见隐藏注<sup><a epub:type="noteref" href="#fn_aside"> <img src="icon.png" alt="' + long_alt + '"/></a></sup>，'
            '以及跨文件注<a class="noteref" href="notes.xhtml#n3"><sup>[3]</sup></a>。</p>'
            '<p><a href="notes.xhtml#n3_next">第一章 这是一条独立占满整行的普通目录超链接，长度超过三十字不应被当作脚注弹窗</a></p>'
            + filler
            + '<p>页底段落附带内联锚点注<sup><a href="notes.xhtml#fn4">[4]</a></sup>，用于验证底部标位触发居顶弹窗。</p>'
            + "".join(f"<p>后文填充段落 {i}，用于将同章文末脚注推到后续页码。</p>" for i in range(1, 45))
            + f'<p id="fn1">{long_note} <a href="#ref1">↩</a></p>'
            + '<aside epub:type="footnote" id="fn_aside"><p>这是隐藏在 EPUB3 aside 中的脚注内容。</p></aside>'
            "</body></html>"
        )
        notes = (
            '<?xml version="1.0" encoding="utf-8"?>'
            '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">'
            "<head><title>注释</title></head>"
            "<body>"
            '<h2>全书注释</h2>'
            '<a id="n3"></a><p>跨文件独立注释页中的第三条注释，使用空锚点前置写法。</p>'
            '<p id="n3_next">下一条独立段落不应混入第三条注释。</p>'
            '<p><a id="fn4"></a>第四条内联锚点注释正文。<a href="ch1.xhtml#ref1">返回</a></p>'
            "</body></html>"
        )
        z.writestr("OPS/ch1.xhtml", ch1)
        z.writestr("OPS/notes.xhtml", notes)

    r = subprocess.run([sys.argv[1], str(book), str(state), sys.argv[2]], capture_output=True, timeout=60)
    assert r.returncode == 0, (r.stdout.decode(errors="replace"), r.stderr.decode(errors="replace"))
    print(r.stdout.decode(errors="replace"))
