"""品牌图的透明裁边、比例、灰阶和输入失败边界。 / Brand-art transparency trimming, proportions, grayscale and input failure boundaries."""
import hashlib
import json
import pathlib
import re
import struct
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / 'tools'))
from build_brand_image import generate


def png(width, height, pixel):
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data))
    rows = b''.join(b'\0' + b''.join(bytes(pixel(x, y)) for x in range(width)) for y in range(height))
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')


class BrandImage(unittest.TestCase):
    def test_transparency_proportion_and_reproducibility(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = root / 'logo.png'
            source.write_bytes(png(12, 8, lambda x, y: (0, 0, 0, 255 if x < 6 else 128) if 2 <= x < 10 and 2 <= y < 6 else (0, 0, 0, 0)))
            before = hashlib.sha256(source.read_bytes()).hexdigest()
            first = generate(source, root / 'a.c', root / 'report.json')
            second = generate(source, root / 'b.c')
            self.assertEqual(first, second)
            self.assertEqual((root / 'a.c').read_bytes(), (root / 'b.c').read_bytes())
            self.assertEqual(first['visible_bounds'], [2, 2, 10, 6])
            self.assertEqual(first['image_size'], [560, 280])
            self.assertEqual(first['position'], [62, 468])
            self.assertEqual(before, hashlib.sha256(source.read_bytes()).hexdigest())
            self.assertEqual(json.loads((root / 'report.json').read_text()), first)
            body = (root / 'a.c').read_text().split('pixels[]={', 1)[1].split('}', 1)[0]
            pixels = bytes(map(int, re.findall(r'\d+', body)))
            self.assertEqual(pixels[10], 0)
            self.assertEqual(pixels[200], 0x77)

    def test_invalid_or_invisible_source_never_creates_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            valid = png(2, 2, lambda x, y: (0, 0, 0, 255))
            corrupt = valid[:-1] + bytes([valid[-1] ^ 1])
            for content in (corrupt, png(2, 2, lambda x, y: (0, 0, 0, 0)), valid[:-3]):
                source = root / 'bad.png'
                source.write_bytes(content)
                with self.assertRaises(ValueError):
                    generate(source, root / 'output.c')
                self.assertFalse((root / 'output.c').exists())


if __name__ == '__main__':
    unittest.main()
