"""只读分析本机EPUB并比对同源C解压器，不输出正文。 / Inspect local EPUB and compare the shared C decoder without printing prose."""
import argparse
import collections
import hashlib
import json
import pathlib
import posixpath
import re
import subprocess
import time
import urllib.parse
import xml.etree.ElementTree as ET
import zipfile


def digest(path):
    # 分块摘要，避免把整本书载入Python内存。/ Hash incrementally without loading the whole book.
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def local_name(element):
    return element.tag.rsplit("}", 1)[-1]


def resolve(base, href):
    reference = urllib.parse.urlsplit(href)
    if reference.scheme or reference.netloc or reference.query:
        raise ValueError("External or queried resource reference")
    decoded = urllib.parse.unquote(reference.path, errors="strict")
    if decoded.startswith("/") or "\\" in decoded or "\x00" in decoded:
        raise ValueError("Invalid resource path")
    target = posixpath.normpath(posixpath.join(posixpath.dirname(base), decoded))
    if target == ".." or target.startswith("../"):
        raise ValueError("Resource escapes container")
    return target


def inspect(archive, binary, chunk=4096, all_resources=False):
    archive = pathlib.Path(archive)
    before = digest(archive)
    started = time.monotonic()
    with zipfile.ZipFile(archive) as book:
        entries = book.infolist()
        if len({entry.filename for entry in entries}) != len(entries):
            raise ValueError("Duplicate ZIP member")
        # ET不解析外部实体；仅作本机测试oracle，非设备出版物解析器。/ ET does not resolve external entities; this is a host oracle, not the device parser.
        container = ET.fromstring(book.read("META-INF/container.xml"))
        package_path = next(x.attrib["full-path"] for x in container.iter() if local_name(x) == "rootfile")
        package = ET.fromstring(book.read(package_path))
        manifest = {x.attrib["id"]: x.attrib for x in package.iter() if local_name(x) == "item"}
        if len(manifest) != sum(local_name(x) == "item" for x in package.iter()):
            raise ValueError("Duplicate manifest id")
        spine = [x.attrib["idref"] for x in package.iter() if local_name(x) == "itemref"]
        spine_paths = [resolve(package_path, manifest[item]["href"]) for item in spine]
        names = {entry.filename for entry in entries}
        for item in manifest.values():
            if resolve(package_path, item["href"]) not in names:
                raise ValueError("Manifest resource missing")
        nav_paths = [resolve(package_path, item["href"]) for item in manifest.values()
                     if item.get("media-type") == "application/x-dtbncx+xml" or "nav" in item.get("properties", "").split()]
        nav_count = 0
        missing_nav = []
        for path in nav_paths:
            nav = ET.fromstring(book.read(path))
            nav_count += sum(local_name(x) == "navPoint" for x in nav.iter())
            for element in nav.iter():
                href = element.attrib.get("src") if local_name(element) == "content" else None
                if href and resolve(path, href) not in names:
                    missing_nav.append(resolve(path, href))
        cover_ids = [x.attrib.get("content") for x in package.iter()
                     if local_name(x) == "meta" and x.attrib.get("name") == "cover"]
        cover_declared = [resolve(package_path, item["href"]) for item in manifest.values()
                          if item["id"] in cover_ids or "cover-image" in item.get("properties", "").split()]
        # 文件名仅是候选，不能冒充出版物的封面声明。/ Filename matches are candidates, not publication cover declarations.
        cover_candidates = [resolve(package_path, item["href"]) for item in manifest.values()
                            if item.get("media-type", "").startswith("image/") and "cover" in item["href"].lower()]
        selected = {"mimetype", "META-INF/container.xml", package_path, *nav_paths, *cover_declared, *cover_candidates}
        if spine_paths:
            selected.update([spine_paths[0], spine_paths[len(spine_paths)//2], spine_paths[-1]])
            selected.add(max(spine_paths, key=lambda path: book.getinfo(path).file_size))
        selected.add(max((entry for entry in entries if not entry.is_dir()), key=lambda entry: entry.file_size).filename)
        if all_resources:
            selected = {entry.filename for entry in entries if not entry.is_dir()}
        peak = 0
        checked_bytes = 0
        for index, name in enumerate(sorted(selected), 1):
            result = subprocess.run([str(binary), str(archive), name, str(chunk)], capture_output=True, timeout=60)
            stats = re.search(rb"zip status=0 verified=1 peak=(\d+) used=0 live=0 attempts=\d+", result.stderr)
            if result.returncode or not stats:
                raise RuntimeError(f"C decoder failed for {name}: {result.stderr.decode(errors='replace')[:1000]}")
            expected = book.read(name)
            if result.stdout != expected:
                raise RuntimeError(f"Decoded byte mismatch: {name}")
            peak = max(peak, int(stats[1]))
            checked_bytes += len(expected)
            if index % 100 == 0:
                print(f"Compared {index}/{len(selected)} resources", flush=True)
        report = {
            "archive": archive.name, "source_sha256": before, "archive_bytes": archive.stat().st_size,
            "epub_version": package.attrib.get("version"), "package_path": package_path,
            "mimetype_valid": book.read("mimetype") == b"application/epub+zip",
            "zip_entries": len(entries), "declared_unpacked_bytes": sum(x.file_size for x in entries),
            "compression_methods": dict(collections.Counter(x.compress_type for x in entries)),
            "manifest_items": len(manifest), "spine_items": len(spine), "ncx_navpoints": nav_count,
            "missing_navigation_resources": missing_nav, "cover_declared": cover_declared,
            "cover_filename_candidates": cover_candidates,
            "media_types": dict(collections.Counter(x.get("media-type") for x in manifest.values())),
            "max_spine_bytes": max((book.getinfo(path).file_size for path in spine_paths), default=0),
            "compared_resources": len(selected), "compared_bytes": checked_bytes,
            "decoder_peak_bytes": peak, "chunk_bytes": chunk, "all_resources": all_resources,
            "elapsed_host_seconds": round(time.monotonic() - started, 3),
            "scope": "ZIP byte oracle only; no device EPUB rendering, fonts, covers or semantic position validation",
        }
    if digest(archive) != before:
        raise RuntimeError("Source changed during test")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=pathlib.Path)
    parser.add_argument("--binary", type=pathlib.Path, required=True)
    parser.add_argument("--chunk", type=int, choices=[1, 17, 4096, 8192], default=4096)
    parser.add_argument("--all", action="store_true", help="Compare every regular ZIP resource")
    parser.add_argument("--report", type=pathlib.Path)
    args = parser.parse_args()
    if args.report and args.report.resolve() == args.archive.resolve():
        parser.error("Report must not replace source EPUB")
    report = inspect(args.archive, args.binary.resolve(), args.chunk, args.all)
    output = json.dumps(report, ensure_ascii=False, indent=2)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(output + "\n", encoding="utf-8")
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
