"""只读字体元数据和可选EPUB正文字符覆盖，生成本机probe输入。 / Inspect font metadata and optional EPUB body coverage, producing local probe input."""
import argparse
import hashlib
import json
import pathlib
import zipfile
import xml.etree.ElementTree as ET
from fontTools.ttLib import TTFont


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("font", type=pathlib.Path)
    parser.add_argument("--epub", type=pathlib.Path)
    parser.add_argument("--output-dir", type=pathlib.Path, default=pathlib.Path("build-dev/font-sample"))
    args = parser.parse_args()
    before = digest(args.font)
    with TTFont(args.font, lazy=True) as font:
        cmap = font.getBestCmap()
        if not cmap:
            parser.error("Font has no supported Unicode cmap")
        report = {"font": args.font.name, "bytes": args.font.stat().st_size, "source_sha256": before,
                  "family": font["name"].getDebugName(1), "style": font["name"].getDebugName(2),
                  "version": font["name"].getDebugName(5), "license": font["name"].getDebugName(13),
                  "unicode_mappings": len(cmap), "glyphs": font["maxp"].numGlyphs,
                  "glyf": "glyf" in font, "variable": "fvar" in font}
        codepoints = "".join(f"{cp:x}\n" for cp in sorted(cmap))
    if args.epub:
        epub_before = digest(args.epub)
        chars = set()
        malformed = []
        chapters = 0
        with zipfile.ZipFile(args.epub) as book:
            for name in book.namelist():
                if not name.lower().endswith((".xhtml", ".html", ".htm")):
                    continue
                try:
                    root = ET.fromstring(book.read(name))
                    body = next((x for x in root.iter() if x.tag.rsplit("}", 1)[-1] == "body"), None)
                    if body is not None:
                        chars.update(ord(c) for c in "".join(body.itertext()) if not c.isspace())
                        chapters += 1
                except ET.ParseError:
                    malformed.append(name)
        report.update({"epub": args.epub.name, "epub_sha256": epub_before, "parsed_body_files": chapters,
                       "novel_unique_body_characters": len(chars), "missing_body_count": len(chars - set(cmap)),
                       "missing_body_characters": [f"U+{cp:04X} {chr(cp)}" for cp in sorted(chars - set(cmap))],
                       "malformed_xhtml": malformed})
        if digest(args.epub) != epub_before:
            raise RuntimeError("EPUB changed during inspection")
    if digest(args.font) != before:
        raise RuntimeError("Font changed during inspection")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    (args.output_dir / "codepoints.txt").write_text(codepoints, encoding="ascii")
    (args.output_dir / "metadata.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    # ASCII终端输出防止Windows旧控制台转码失真；报告仍为UTF-8。/ ASCII console output avoids legacy Windows encoding loss; the report remains UTF-8.
    print(json.dumps(report, ensure_ascii=True, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
