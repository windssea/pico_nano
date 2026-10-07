"""验证文档链接、复用摘要与16MiB分区。 / Verify links, vendor hashes and 16MiB partitions."""
import csv
import hashlib
import io
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]


def check_partitions(content):
    ranges = []
    names = set()
    for row in csv.reader(io.StringIO(content)):
        if not row or row[0].strip().startswith("#"):
            continue
        if len(row) < 5:
            raise ValueError("Partition row is missing fields")
        name, kind, _, offset, size = [item.strip() for item in row[:5]]
        start, length = int(offset, 0), int(size, 0)
        if name in names or start < 0x9000 or length <= 0 or start + length > 0x1000000:
            raise ValueError(f"Invalid partition bounds/name: {name}")
        if start % (0x10000 if kind == "app" else 0x1000) or length % 0x1000:
            raise ValueError(f"Invalid partition alignment: {name}")
        names.add(name)
        ranges.append((start, start + length, name))
    if not ranges:
        raise ValueError("No partitions defined")
    ranges.sort()
    for earlier, later in zip(ranges, ranges[1:]):
        if earlier[1] > later[0]:
            raise ValueError(f"Overlapping partitions: {earlier[2]} and {later[2]}")


def check_markdown(root):
    errors = []
    docs = [root / "README.md"] if (root / "README.md").exists() else []
    docs += list((root / "docs").rglob("*.md")) if (root / "docs").exists() else list(root.glob("*.md"))
    for path in sorted(set(docs)):
        if "local" in path.relative_to(root).parts:
            continue
        content = path.read_text(encoding="utf-8")
        if sum(line.startswith("```") for line in content.splitlines()) % 2:
            errors.append(f"Unclosed code fence: {path}")
        for link in re.findall(r"\]\(([^)]+)\)", content):
            if "://" in link or link.startswith("#"):
                continue
            destination = link.split("#", 1)[0]
            if destination and not (path.parent / destination).exists():
                errors.append(f"Missing target: {path}: {link}")
    return errors


def main():
    errors = check_markdown(ROOT)
    try:
        check_partitions((ROOT / "partitions.csv").read_text(encoding="utf-8"))
    except ValueError as exc:
        errors.append(str(exc))
    manifest = json.loads((ROOT / "LICENSES/reference-manifest.json").read_text(encoding="utf-8"))
    for entry in manifest["files"]:
        path = ROOT / entry["path"]
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != entry["sha256"]:
            errors.append(f"Vendor source changed or missing: {entry['path']}")
    for name in {pathlib.PurePosixPath(item["path"]).parts[1] for item in manifest["files"]}:
        if not (ROOT / "components" / name / "LICENSE").is_file():
            errors.append(f"Missing license: {name}")
    ft_manifest = json.loads((ROOT / "LICENSES/freetype-manifest.json").read_text(encoding="utf8"))
    z_manifest = json.loads((ROOT / "LICENSES/zlib-manifest.json").read_text(encoding="utf8"))
    xml_manifest = json.loads((ROOT / "LICENSES/expat-manifest.json").read_text(encoding="utf8"))
    png_manifest = json.loads((ROOT / "LICENSES/spng-manifest.json").read_text(encoding="utf8"))
    jpeg_manifest = json.loads((ROOT / "LICENSES/jpeg-manifest.json").read_text(encoding="utf8"))
    for relative, digest in jpeg_manifest["files"].items():
        file = ROOT / "components/pn_jpeg/vendor" / relative
        if not file.is_file() or hashlib.sha256(file.read_bytes()).hexdigest() != digest:
            errors.append(f"libjpeg-turbo source changed: {relative}")
    for license_file in ["LICENSE.md", "README.ijg"]:
        if not (ROOT / "components/pn_jpeg/vendor" / license_file).is_file():
            errors.append(f"JPEG license missing: {license_file}")
    for relative, digest in png_manifest["files"].items():
        file = ROOT / "components/pn_spng/vendor" / relative
        if not file.is_file() or hashlib.sha256(file.read_bytes()).hexdigest() != digest:
            errors.append(f"libspng source changed: {relative}")
    if not (ROOT / "components/pn_spng/vendor/LICENSE").is_file():
        errors.append("libspng license missing")
    for relative, digest in xml_manifest["files"].items():
        file = ROOT / "components/pn_expat/vendor" / relative
        if not file.is_file() or hashlib.sha256(file.read_bytes()).hexdigest() != digest:
            errors.append(f"Expat source changed: {relative}")
    if not (ROOT / "components/pn_expat/vendor/COPYING").is_file():
        errors.append("Expat license missing")
    for relative, digest in z_manifest["files"].items():
        file = ROOT / "components/pn_zlib/vendor" / relative
        if not file.is_file() or hashlib.sha256(file.read_bytes()).hexdigest() != digest:
            errors.append(f"zlib source changed: {relative}")
    for relative, digest in ft_manifest["files"].items():
        file = ROOT / "components/pn_freetype/vendor" / relative
        if not file.is_file() or hashlib.sha256(file.read_bytes()).hexdigest() != digest:
            errors.append(f"FreeType source changed: {relative}")
    lfs_manifest = json.loads((ROOT / "LICENSES/littlefs-manifest.json").read_text(encoding="utf8"))
    for relative, digest in lfs_manifest["files"].items():
        file = ROOT / "components/pn_littlefs/vendor" / relative
        if not file.is_file() or hashlib.sha256(file.read_bytes()).hexdigest() != digest:
            errors.append(f"LittleFS source changed: {relative}")
    font_manifest = json.loads((ROOT / "LICENSES/ui-font-manifest.json").read_text(encoding="utf8"))
    if hashlib.sha256((ROOT / "assets/fonts/read-pico-ui.ttf").read_bytes()).hexdigest() != font_manifest["output_sha256"]:
        errors.append("UI font hash differs")
    if not (ROOT / "assets/fonts/OFL.txt").is_file() or hashlib.sha256((ROOT / "assets/fonts/OFL.txt").read_bytes()).hexdigest() != font_manifest["ofl_sha256"]:
        errors.append("UI font OFL missing")
    fixture = json.loads((ROOT / "LICENSES/font-fixture-manifest.json").read_text(encoding="utf8"))
    if fixture["source_sha256"] != font_manifest["source_sha256"] or fixture["license"] != "OFL-1.1" or hashlib.sha256((ROOT / "tests/fixtures/font-fallback.ttf").read_bytes()).hexdigest() != fixture["output_sha256"]:
        errors.append("Fallback fixture source/license/hash differs")
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print(f"Documentation links/fences, partitions and {len(manifest['files'])} reference hashes/licenses, 744 FreeType, 116 LittleFS, {len(z_manifest['files'])} zlib, {len(xml_manifest['files'])} Expat, {len(png_manifest['files'])} libspng, {len(jpeg_manifest['files'])} libjpeg-turbo sources and font hashes passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
