"""构建时裁去透明留边并生成原生4bpp品牌图，不修改原图。 / Trim transparent margins and generate native 4bpp brand art at build time without modifying its source."""
import argparse
import hashlib
import json
import math
import pathlib
import struct
import zlib


def decode(path):
    """只接受有界、非隔行8位RGB/RGBA构建输入。 / Accept bounded, noninterlaced 8-bit RGB/RGBA build inputs only."""
    data = path.read_bytes()
    if len(data) > 16 * 1024 * 1024 or data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('Invalid PNG source')
    at = 8
    compressed = bytearray()
    width = height = channels = 0
    ended = False
    while at + 12 <= len(data):
        size = struct.unpack_from('>I', data, at)[0]
        end = at + 12 + size
        if end > len(data):
            raise ValueError('Truncated PNG chunk')
        tag = data[at + 4:at + 8]
        payload = data[at + 8:at + 8 + size]
        crc = struct.unpack_from('>I', data, at + 8 + size)[0]
        if zlib.crc32(tag + payload) & 0xffffffff != crc:
            raise ValueError('PNG CRC mismatch')
        if tag == b'IHDR':
            if width or size != 13:
                raise ValueError('Invalid PNG header')
            width, height, depth, color, compression, filtering, interlace = struct.unpack('>IIBBBBB', payload)
            if not (0 < width <= 4096 and 0 < height <= 4096) or depth != 8 or color not in (2, 6) or compression or filtering or interlace:
                raise ValueError('Expected bounded noninterlaced RGB/RGBA PNG')
            channels = 4 if color == 6 else 3
        elif tag == b'IDAT':
            compressed.extend(payload)
        elif tag == b'IEND':
            ended = size == 0 and end == len(data)
            break
        elif tag[:1].isupper() and tag not in (b'PLTE',):
            raise ValueError('Unsupported critical PNG chunk')
        at = end
    if not ended or not width or not compressed:
        raise ValueError('Incomplete PNG')
    row_bytes = width * channels
    expected = (row_bytes + 1) * height
    inflater = zlib.decompressobj()
    raw = inflater.decompress(compressed, expected + 1)
    if len(raw) != expected or not inflater.eof or inflater.unused_data or inflater.unconsumed_tail:
        raise ValueError('Invalid PNG scanline stream')
    gray = bytearray(width * height)
    previous = bytearray(row_bytes)
    xmin, ymin, xmax, ymax = width, height, -1, -1
    for y in range(height):
        pos = y * (row_bytes + 1)
        method = raw[pos]
        row = bytearray(raw[pos + 1:pos + 1 + row_bytes])
        if method > 4:
            raise ValueError('Invalid PNG filter')
        for x in range(row_bytes):
            left = row[x - channels] if x >= channels else 0
            above = previous[x]
            corner = previous[x - channels] if x >= channels else 0
            if method == 1:
                prediction = left
            elif method == 2:
                prediction = above
            elif method == 3:
                prediction = (left + above) // 2
            elif method == 4:
                p = left + above - corner
                distances = abs(p - left), abs(p - above), abs(p - corner)
                prediction = (left, above, corner)[distances.index(min(distances))]
            else:
                prediction = 0
            row[x] = (row[x] + prediction) & 255
        for x in range(width):
            i = x * channels
            alpha = row[i + 3] if channels == 4 else 255
            luminance = (77 * row[i] + 150 * row[i + 1] + 29 * row[i + 2] + 128) // 256
            value = (luminance * alpha + 255 * (255 - alpha) + 127) // 255
            gray[y * width + x] = value
            if value < 250:
                xmin, xmax = min(xmin, x), max(xmax, x)
                ymin, ymax = min(ymin, y), max(ymax, y)
        previous = row
    if xmax < xmin:
        raise ValueError('PNG has no visible artwork on white')
    return gray, width, height, (xmin, ymin, xmax + 1, ymax + 1)


def weights(source, target):
    scale = source / target
    result = []
    for i in range(target):
        begin, end = i * scale, (i + 1) * scale
        result.append([(j, (min(end, j + 1) - max(begin, j)) / scale)
                       for j in range(math.floor(begin), min(source, math.ceil(end)))])
    return result


def generate(source, output, report=None):
    gray, sw, sh, box = decode(source)
    x0, y0, x1, y1 = box
    cw, ch = x1 - x0, y1 - y0
    # 四像素宽对齐便于半字节直接拷贝；安全留边保留整幅字标。/ Four-pixel alignment permits nibble-aligned copies; safe margins retain the whole wordmark.
    scale = min(560 / cw, 900 / ch)
    width = max(4, int(cw * scale) // 4 * 4)
    height = max(1, round(ch * width / cw))
    horizontal = weights(cw, width)
    vertical = weights(ch, height)
    rows = [[sum(gray[(y + y0) * sw + x0 + x] * weight for x, weight in cell)
             for cell in horizontal] for y in range(ch)]
    pixels = bytearray(width // 2 * height)
    for y, cell in enumerate(vertical):
        for x in range(width):
            value = sum(rows[j][x] * weight for j, weight in cell)
            nibble = max(0, min(15, round(value * 15 / 255)))
            pixels[y * (width // 2) + x // 2] |= nibble << (4 if x & 1 else 0)
    lines = ['/* Generated by tools/build_brand_image.py; do not edit. */', '#include <stdint.h>',
             f'const unsigned pn_brand_image_width={width},pn_brand_image_height={height};',
             'const uint8_t pn_brand_image_pixels[]={']
    lines.extend(' ' + ','.join(str(v) for v in pixels[i:i + 32]) + ',' for i in range(0, len(pixels), 32))
    lines.extend(['};', ''])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('\n'.join(lines), encoding='utf-8')
    metadata = {'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(), 'source_size': [sw, sh],
                'visible_bounds': list(box), 'screen_size': [684, 1216], 'image_size': [width, height],
                'position': [(684 - width) // 2, (1216 - height) // 2], 'format': '4bpp, white composite, area resampling',
                'bytes': len(pixels), 'pixels_sha256': hashlib.sha256(pixels).hexdigest()}
    if report:
        report.parent.mkdir(parents=True, exist_ok=True)
        report.write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    return metadata


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=pathlib.Path)
    parser.add_argument('output', type=pathlib.Path)
    parser.add_argument('--report', type=pathlib.Path)
    args = parser.parse_args()
    print(json.dumps(generate(args.source, args.output, args.report)))
