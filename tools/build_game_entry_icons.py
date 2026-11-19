"""规范化小图标并生成有界只读数据。 / Normalize small icons and emit bounded read-only data."""
import argparse
import hashlib
import json
import struct
import zlib
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    Image = None

ROOT = Path(__file__).resolve().parents[1]
NAMES = ("sudoku", "sokoban", "minesweeper", "nonogram", "xiangqi", "klotski", "tsumego")
SIDE = 64
BYTES_PER_ICON = SIDE * SIDE // 4
TOTAL_BUDGET = len(NAMES) * BYTES_PER_ICON


def normalize(source, output):
    """白底合成、包围盒居中及四阶整数像素网格。 / Composite on white, center bounds and use a four-level integer grid."""
    if Image is None:
        raise RuntimeError("Pillow is required when --sources normalization is requested")
    with Image.open(source) as opened:
        rgba = opened.convert("RGBA")
        paper = Image.new("RGBA", rgba.size, "white")
        paper.alpha_composite(rgba)
        gray = paper.convert("L")
    bounds = gray.point(lambda value: 255 if value < 220 else 0).getbbox()
    if bounds is None:
        raise ValueError(f"Empty icon: {source}")
    cropped = gray.crop(bounds)
    cropped.thumbnail((60, 60), Image.Resampling.NEAREST)
    grid = Image.new("L", (SIDE, SIDE), 255)
    grid.paste(cropped, ((SIDE - cropped.width) // 2, (SIDE - cropped.height) // 2))
    indexed = Image.new("P", grid.size)
    indexed.putpalette([value for gray_value in (0, 85, 170, 255) for value in (gray_value,) * 3])
    indexed.putdata([(value + 42) // 85 for value in grid.tobytes()])
    output.parent.mkdir(parents=True, exist_ok=True)
    indexed.save(output, optimize=True, bits=2)
    if output.stat().st_size > 1024:
        raise ValueError(f"Icon exceeds 1 KiB PNG budget: {output}")


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else (b if pb <= pc else c)


def _read_png_gray_stdlib(path: Path):
    """纯标准库读取已规范化PNG灰度字节，无Pillow环境亦可复建。 / Read normalized PNG grayscale bytes with stdlib so builds work without Pillow."""
    raw = path.read_bytes()
    if raw[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"Not a PNG file: {path}")
    pos = 8
    width = height = bit_depth = color_type = interlace = 0
    palette = None
    idat = bytearray()
    while pos < len(raw):
        length = struct.unpack(">I", raw[pos:pos + 4])[0]
        tag = raw[pos + 4:pos + 8]
        chunk_data = raw[pos + 8:pos + 8 + length]
        crc = struct.unpack(">I", raw[pos + 8 + length:pos + 8 + length + 4])[0]
        if (zlib.crc32(tag + chunk_data) & 0xFFFFFFFF) != crc:
            raise ValueError(f"Corrupt PNG CRC in {path}")
        pos += 12 + length
        if tag == b"IHDR":
            width, height, bit_depth, color_type, comp, filt, interlace = struct.unpack(">IIBBBBB", chunk_data)
            if comp != 0 or filt != 0 or interlace != 0:
                raise ValueError(f"Unsupported PNG compression/interlace: {path}")
        elif tag == b"PLTE":
            palette = [chunk_data[i:i + 3] for i in range(0, len(chunk_data), 3)]
        elif tag == b"IDAT":
            idat.extend(chunk_data)
        elif tag == b"IEND":
            break
    if (width, height) != (SIDE, SIDE):
        raise ValueError(f"Unexpected dimensions: {path}")
    if color_type not in (0, 3) or bit_depth not in (1, 2, 4, 8):
        raise ValueError(f"Unsupported PNG format in {path}")
    stride = (width * bit_depth + 7) // 8
    bpp = 1
    decompressed = zlib.decompress(bytes(idat))
    if len(decompressed) != height * (1 + stride):
        raise ValueError(f"Unexpected PNG scanline length: {path}")
    prev = bytearray(stride)
    pixels = []
    mask = (1 << bit_depth) - 1
    per_byte = 8 // bit_depth
    for row_idx in range(height):
        base = row_idx * (1 + stride)
        ftype = decompressed[base]
        cur = bytearray(decompressed[base + 1:base + 1 + stride])
        for i in range(stride):
            left = cur[i - bpp] if i >= bpp else 0
            up = prev[i]
            up_left = prev[i - bpp] if i >= bpp else 0
            if ftype == 1:
                cur[i] = (cur[i] + left) & 0xFF
            elif ftype == 2:
                cur[i] = (cur[i] + up) & 0xFF
            elif ftype == 3:
                cur[i] = (cur[i] + ((left + up) >> 1)) & 0xFF
            elif ftype == 4:
                cur[i] = (cur[i] + _paeth(left, up, up_left)) & 0xFF
            elif ftype != 0:
                raise ValueError(f"Invalid PNG filter {ftype}: {path}")
        prev = cur
        count = 0
        for b in cur:
            for slot in range(per_byte):
                if count >= width:
                    break
                idx = (b >> (8 - bit_depth * (slot + 1))) & mask
                if color_type == 3:
                    if palette is None or idx >= len(palette):
                        raise ValueError(f"Invalid palette index in {path}")
                    r, g, bl = palette[idx]
                    if r != g or g != bl:
                        raise ValueError(f"Non-grayscale palette in {path}")
                    pixels.append(r)
                else:
                    pixels.append((idx * 255) // mask)
                count += 1
    return pixels


def pack(path):
    """只接受64方形四阶资源；每字节先写高位像素。 / Accept only 64-square four-level assets, packing high pixels first."""
    if path.stat().st_size > 1024:
        raise ValueError(f"Icon exceeds 1 KiB PNG budget: {path}")
    if Image is not None:
        with Image.open(path) as opened:
            if opened.size != (SIDE, SIDE):
                raise ValueError(f"Unexpected dimensions: {path}")
            pixels = list(opened.convert("L").tobytes())
    else:
        pixels = _read_png_gray_stdlib(path)
    if not set(pixels) <= {0, 85, 170, 255}:
        raise ValueError(f"Unexpected grayscale palette: {path}")
    result = bytearray()
    for at in range(0, len(pixels), 4):
        values = [value // 85 for value in pixels[at:at + 4]]
        result.append((values[0] << 6) | (values[1] << 4) | (values[2] << 2) | values[3])
    assert len(result) == BYTES_PER_ICON
    return bytes(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sources", type=Path, help="Optional imagegen originals; named sudoku/sokoban/minesweeper/nonogram/xiangqi.png")
    parser.add_argument("--assets", type=Path, default=ROOT / "assets/games/entry-icons")
    parser.add_argument("--output", type=Path, default=ROOT / "components/pn_library/pn_game_entry_icons.inc")
    parser.add_argument("--update-manifest", action="store_true", help="Refresh manifest.json preserving existing source hashes")
    args = parser.parse_args()
    manifest_path = args.assets / "manifest.json"
    existing_sources = {}
    if manifest_path.is_file():
        old = json.loads(manifest_path.read_text(encoding="utf-8"))
        for item in old.get("icons", []):
            if "imagegen_source_sha256" in item:
                existing_sources[item["name"]] = item["imagegen_source_sha256"]
    records, blobs = [], []
    for name in NAMES:
        target = args.assets / f"{name}.png"
        source = args.sources / f"{name}.png" if args.sources and (args.sources / f"{name}.png").is_file() else None
        if source is not None:
            normalize(source, target)
        data = pack(target)
        blobs.append(data)
        record = {"name": name, "file": target.name, "width": SIDE, "height": SIDE, "levels": 4,
                  "png_bytes": target.stat().st_size, "packed_bytes": len(data),
                  "png_sha256": hashlib.sha256(target.read_bytes()).hexdigest(),
                  "packed_sha256": hashlib.sha256(data).hexdigest()}
        if source is not None:
            record["imagegen_source_sha256"] = hashlib.sha256(source.read_bytes()).hexdigest()
        elif name in existing_sources:
            record["imagegen_source_sha256"] = existing_sources[name]
        records.append(record)
    rows = ["/* 由tools/build_game_entry_icons.py生成，不手改。 / Generated by tools/build_game_entry_icons.py; do not edit. */",
            f"static const uint8_t pn_game_entry_icon_data[{len(NAMES)}][{BYTES_PER_ICON}] = {{"]
    for name, data in zip(NAMES, blobs):
        rows.append(f"    /* {name} */\n    {{")
        rows.extend("        " + ",".join(f"0x{value:02x}" for value in data[at:at + 32]) + "," for at in range(0, len(data), 32))
        rows.append("    },")
    rows.extend(["};", f'_Static_assert(sizeof pn_game_entry_icon_data == {TOTAL_BUDGET}, "Game entry icon budget");', ""])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(rows), encoding="utf-8")
    manifest = {"generator": "Original pixel-art & OpenAI image_gen; deterministic grid normalization by build_game_entry_icons.py",
                "usage": "Original game-launcher artwork; no external source artwork", "palette_4bpp": [0, 5, 10, 15],
                "pixel_order": "High two bits first, row-major", "display_scale": 2, "total_packed_bytes": sum(map(len, blobs)),
                "icons": records}
    if args.sources or args.update_manifest:
        manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"packed_bytes": manifest["total_packed_bytes"], "png_bytes": sum(row["png_bytes"] for row in records), "output": str(args.output)}))


if __name__ == "__main__":
    main()
