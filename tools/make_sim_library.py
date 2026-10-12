"""生成原创测试书库；私人小说只复制到忽略目录。 / Generate an original test library; copy private novels only into ignored output."""
import argparse
import html
import hashlib
import json
import pathlib
import shutil
import struct
import zipfile
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
PARAGRAPHS = [
    "清晨，窗外的风吹过树梢。她打开书，接着昨天的段落继续阅读。这个故事用于测试中文排版、首行缩进与阅读位置保存。",
    "“今天从哪里开始？”他问。她指了指书签，说：“从这里。翻页以后，还可以回到刚才的位置。”",
    "行间距、段落间距和字间距各有作用。调整参数后，请观察预览，再选择应用或取消，检查文字是否清晰、是否出现截断。",
    "数字测试：2026 年，1234567890。标点测试：，。！？；：（）【】《》……——。English words and numbers help test mixed typography.",
]


def cover_png():
    """原创条纹封面，无额外图像依赖。 / Original striped cover without image dependencies."""
    w, h = 280, 400
    raw = b"".join(b"\0" + bytes(35 if y < 65 else 220 if (x // 35 + y // 50) % 2 else 165 for x in range(w)) for y in range(h))
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")


def wallpaper_png():
    """原生尺寸透明几何壁纸，CC0，无额外依赖。 / Native-size transparent geometric wallpaper, CC0, without extra dependencies."""
    w, h = 684, 1216
    rows = []
    for y in range(h):
        row = bytearray(b"\0")
        for x in range(w):
            shade = 35 if x < 80 + y // 4 else 190
            alpha = 200 if 160 < y < 1000 else 0
            row.extend((shade, shade, shade, alpha))
        rows.append(row)
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(b"".join(rows))) + chunk(b"IEND", b"")


def epub(path, title, version, illustrated=False):
    """包含真实章节目录的EPUB2/3。 / EPUB2/3 with actual chapter navigation."""
    chapters = 5
    esc = html.escape(title)
    items = ''.join(f'<item id="c{i}" href="c{i}.xhtml" media-type="application/xhtml+xml"/>' for i in range(chapters))
    spine = ''.join(f'<itemref idref="c{i}"/>' for i in range(chapters))
    links = ''.join(f'<li><a href="c{i}.xhtml">第{i+1}章 阅读测试</a></li>' for i in range(chapters))
    nav = '<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>' if version == "3.0" else '<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>'
    image = '<item id="cover" href="cover.png" media-type="image/png" properties="cover-image"/>' if illustrated else ''
    modified = '<meta property="dcterms:modified">2026-10-11T00:00:00Z</meta>' if version == "3.0" else ''
    toc = ' toc="ncx"' if version == "2.0" else ''
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        z.writestr("META-INF/container.xml", '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr("OPS/book.opf", f'<package xmlns="http://www.idpf.org/2007/opf" xmlns:d="http://purl.org/dc/elements/1.1/" version="{version}" unique-identifier="uid"><metadata><d:identifier id="uid">pico-demo-{path.stem}</d:identifier><d:title>{esc}</d:title><d:language>zh-CN</d:language><d:creator>PicoNano 示例书库</d:creator><d:rights>CC0-1.0</d:rights>{modified}</metadata><manifest>{items}{nav}{image}</manifest><spine{toc}>{spine}</spine></package>')
        if version == "3.0":
            z.writestr("OPS/nav.xhtml", f'<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><head><title>目录</title></head><body><nav epub:type="toc"><ol>{links}</ol></nav></body></html>')
        else:
            points = ''.join(f'<navPoint id="n{i}" playOrder="{i+1}"><navLabel><text>第{i+1}章 阅读测试</text></navLabel><content src="c{i}.xhtml"/></navPoint>' for i in range(chapters))
            z.writestr("OPS/toc.ncx", f'<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1"><head><meta name="dtb:uid" content="pico-demo-{path.stem}"/></head><docTitle><text>{esc}</text></docTitle><navMap>{points}</navMap></ncx>')
        for i in range(chapters):
            body = (f'<img src="cover.png" alt="测试封面"/>' if illustrated and i == 0 else '')
            body += f'<h1>第{i+1}章 阅读测试</h1>' + ''.join(f'<p>{html.escape(PARAGRAPHS[j % len(PARAGRAPHS)])}</p>' for j in range(64))
            z.writestr(f"OPS/c{i}.xhtml", f'<html xmlns="http://www.w3.org/1999/xhtml"><head><title>第{i+1}章</title></head><body>{body}</body></html>')
        if illustrated:
            z.writestr("OPS/cover.png", cover_png())


def generate(output, private_book=None, local_inputs=None):
    """只补缺失样本，保留本机编辑；生成来源/摘要清单。 / Fill missing samples, preserve local edits, and record provenance/hashes."""
    output.mkdir(parents=True, exist_ok=True)
    manifest = output.parent / "manifest.json"
    try:
        previous = {item["path"]: item for item in json.loads(manifest.read_text(encoding="utf-8"))["files"]}
    except (OSError, ValueError, KeyError, TypeError):
        previous = {}
    records = []
    def record(path, license_name, source, created):
        relative = path.relative_to(output.parent).as_posix()
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        old = previous.get(relative, {})
        if not created and old.get("sha256") == digest:
            license_name, source = old.get("license", "not-declared"), old.get("source", "existing-local-file")
        elif not created:
            license_name, source = "not-declared", "existing-local-file"
        records.append({"path": relative, "sha256": digest, "license": license_name, "source": source})
    def write_missing(path, data, license_name="CC0-1.0", source="tools/make_sim_library.py"):
        created = not path.exists()
        if created:
            path.write_bytes(data)
        record(path, license_name, source, created)
    text = "测试文本与条纹封面为原创，使用CC0-1.0。\n\n" + "\n\n".join(PARAGRAPHS * 90)
    for name, encoding in [("01-中文段落与标点.txt", "utf-8"), ("02-带字节序标记.txt", "utf-8-sig"), ("03-双字节中文.txt", "utf-16"), ("04-传统中文编码.txt", "gbk")]:
        write_missing(output / name, text.encode(encoding))
    write_missing(output / "01-中文段落与标点.png", cover_png())
    for name, version, images in [("05-章节目录与书签.epub", "2.0", False), ("06-新版目录与混合文字.epub", "3.0", False), ("07-封面与插图.epub", "3.0", True), ("08-这是一本用于检查长书名折行和省略显示效果的测试电子书.epub", "2.0", False)]:
        path = output / name
        created = not path.exists()
        if created:
            epub(path, pathlib.Path(name).stem, version, images)
        record(path, "CC0-1.0", "tools/make_sim_library.py", created)
    if private_book and private_book.is_file():
        target = output / private_book.name
        created = not target.exists()
        if target.resolve() != private_book.resolve() and created:
            shutil.copy2(private_book, target)
        record(target, "private-local-only", "optional private sample; original is read-only", created)
    fonts, wallpapers = output.parent / "fonts", output.parent / "wallpapers"
    fonts.mkdir(exist_ok=True)
    wallpapers.mkdir(exist_ok=True)
    # 本机清单只指定来源；目标固定为books/fonts，原件只读、已有副本不覆盖。
    # Local manifests specify sources only; targets are fixed books/fonts, originals are read-only and existing copies are preserved.
    local_inputs = local_inputs or output.parent / "local-inputs.json"
    if local_inputs.is_file():
        for entry in json.loads(local_inputs.read_text(encoding="utf-8"))["files"]:
            kind = entry["kind"]
            original = pathlib.Path(entry["path"])
            if not original.is_absolute():
                original = ROOT / original
            if kind not in ("book", "font") or original.suffix.lower() not in ((".epub", ".txt") if kind == "book" else (".ttf",)):
                raise ValueError("Invalid local sample kind or extension")
            if not original.is_file():
                raise FileNotFoundError(original)
            target = (output if kind == "book" else fonts) / original.name
            if target.resolve() == original.resolve():
                raise ValueError("Local samples must use copies, not originals")
            created = not target.exists()
            if created:
                shutil.copy2(original, target)
            record(target, "private-local-only", "local-inputs.json; original is read-only", created)
    write_missing(fonts / "ReadPicoSample.ttf", (ROOT / "assets/fonts/read-pico-reading.ttf").read_bytes(), "OFL-1.1", "assets/fonts/read-pico-reading.ttf")
    write_missing(fonts / "OFL.txt", (ROOT / "assets/fonts/OFL.txt").read_bytes(), "OFL-1.1", "assets/fonts/OFL.txt")
    write_missing(wallpapers / "01-透明几何.png", wallpaper_png())
    manifest.write_text(json.dumps({"files": records, "note": "Generated inputs only; runtime state is separate. Existing local changes are retained."}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Test library: {output} ({len(list(output.glob('*.txt')))} TXT, {len(list(output.glob('*.epub')))} EPUB)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=pathlib.Path, default=ROOT / "build-dev/sim-library/books")
    parser.add_argument("--private-book", type=pathlib.Path, default=ROOT / "mockDoc/绍宋.epub")
    parser.add_argument("--local-inputs", type=pathlib.Path, help="Local-only book/font source manifest; defaults beside the output books directory")
    args = parser.parse_args()
    generate(args.out, args.private_book, args.local_inputs)
