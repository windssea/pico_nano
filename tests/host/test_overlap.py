"""原创多页中文样本：比较扫描期重叠与无重叠的逐帧结果和前台光栅次数。 / Original multi-page CJK fixtures comparing scan-time overlap against no overlap frame by frame and by foreground raster count."""
import html, pathlib, random, subprocess, sys, tempfile, zipfile
ALPHABET = '天地玄黄宇宙洪荒日月盈昃辰宿列张寒来暑往秋收冬藏闰余成岁律吕调阳云腾致雨露结为霜金生丽水玉出昆冈剑号巨阙珠称夜光果珍李柰菜重芥姜海咸河淡鳞潜羽翔龙师火帝鸟官人皇始制文字乃服衣裳推位让国有虞陶唐吊民伐罪周发殷汤坐朝问道垂拱平章爱育黎首臣伏戎羌遐迩一体率宾归王鸣凤在竹白驹食场化被草木赖及万方'
with tempfile.TemporaryDirectory() as folder:
    rng = random.Random(20261014)
    paragraphs = [''.join(rng.choices(ALPHABET, k=rng.randint(60, 180))) for _ in range(160)]
    book = pathlib.Path(folder) / 'overlap.txt'
    book.write_text('\n'.join(paragraphs), encoding='utf-8')
    epub = pathlib.Path(folder) / 'overlap.epub'
    with zipfile.ZipFile(epub, 'w', compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr('mimetype', 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
        z.writestr('META-INF/container.xml', '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('book.opf', '<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="3.0" unique-identifier="u"><metadata><d:title>Original overlap</d:title><d:identifier id="u">pn-overlap</d:identifier></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="a"/></spine></package>')
        z.writestr('a.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Original</title></head><body>' + ''.join('<p>' + html.escape(p) + '</p>' for p in paragraphs) + '</body></html>')
    subprocess.run([sys.argv[1], str(book), str(epub)], check=True, timeout=300)
