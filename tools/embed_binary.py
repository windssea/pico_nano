"""将已固定资源嵌入构建产物，可选按固定块做原始DEFLATE压缩。 / Embed pinned assets into build output, optionally with fixed-block raw DEFLATE compression."""
import argparse
import hashlib
import pathlib
import zlib


def emit_raw(data, symbol):
    """输出未压缩字节数组与SHA-256。 / Emit uncompressed byte array and SHA-256."""
    rows = ["#include <stddef.h>", "#include <stdint.h>", f"const uint8_t {symbol}_bytes[] = {{"]
    rows.extend("    " + ",".join(str(x) for x in data[i:i+32]) + "," for i in range(0, len(data), 32))
    rows.extend([
        "};",
        f"const size_t {symbol}_size = {len(data)}u;",
        f"const uint8_t {symbol}_sha256[32] = {{" + ",".join(str(x) for x in hashlib.sha256(data).digest()) + "};",
        "",
    ])
    return "\n".join(rows)


def emit_deflate_blocks(data, symbol, block_size):
    """按固定块压缩并逐块回环验真，保持原始长度与SHA-256。 / Compress fixed blocks and round-trip verify every block, preserving original size and SHA-256."""
    if block_size <= 0 or (block_size & (block_size - 1)) != 0:
        raise SystemExit("block_size must be a positive power of two")
    packed = bytearray()
    offsets = [0]
    restored = bytearray()
    for at in range(0, len(data), block_size):
        chunk = data[at:at+block_size]
        comp = zlib.compressobj(level=9, method=zlib.DEFLATED, wbits=-15)
        zchunk = comp.compress(chunk) + comp.flush()
        if zlib.decompress(zchunk, -15) != chunk:
            raise SystemExit("Deflate block round-trip mismatch")
        packed.extend(zchunk)
        offsets.append(len(packed))
        restored.extend(chunk)
    digest = hashlib.sha256(data).digest()
    if bytes(restored) != data or hashlib.sha256(restored).digest() != digest:
        raise SystemExit("Deflate full-file SHA-256 mismatch")
    rows = [
        "#include <stddef.h>",
        "#include <stdint.h>",
        f"const uint8_t {symbol}_zbytes[] = {{",
    ]
    rows.extend("    " + ",".join(str(x) for x in packed[i:i+32]) + "," for i in range(0, len(packed), 32))
    rows.extend(["};", f"const uint32_t {symbol}_zoffsets[] = {{"])
    rows.extend("    " + ",".join(f"{x}u" for x in offsets[i:i+16]) + "," for i in range(0, len(offsets), 16))
    rows.extend([
        "};",
        f"const size_t {symbol}_zblocks = {len(offsets) - 1}u;",
        f"const size_t {symbol}_zblock_size = {block_size}u;",
        f"const size_t {symbol}_size = {len(data)}u;",
        f"const uint8_t {symbol}_sha256[32] = {{" + ",".join(str(x) for x in digest) + "};",
        "",
    ])
    return "\n".join(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("symbol", nargs="?", default="pn_ui_font")
    parser.add_argument("--deflate-blocks", type=int, default=0, help="Compress into fixed raw-DEFLATE blocks of this size")
    args = parser.parse_args()
    if not args.symbol.isidentifier() or not args.symbol.isascii():
        raise SystemExit("Invalid C symbol")
    data = args.input.read_bytes()
    text = emit_deflate_blocks(data, args.symbol, args.deflate_blocks) if args.deflate_blocks else emit_raw(data, args.symbol)
    args.output.write_text(text, encoding="utf8")


if __name__ == "__main__":
    main()
