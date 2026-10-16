"""诊断串口工具的帧还原与PNG输出。 / Frame restoration and PNG output of the diagnostic serial tool."""
import pathlib
import random
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / 'tools'))
from device_test import BOARD_ROW, FRAME_BYTES, HEIGHT, WIDTH, board_to_gray, write_png


def logical_to_board(gray4):
    """独立实现pn_device_frame_copy的布局：逻辑列x→板级行683-x，逻辑行y→字节y/2，偶行低半字节。"""
    board = bytearray(FRAME_BYTES)
    for y in range(HEIGHT):
        for x in range(WIDTH):
            index = (WIDTH - 1 - x) * BOARD_ROW + y // 2
            board[index] |= gray4[y * WIDTH + x] << (4 * (y & 1))
    return bytes(board)


class DeviceTestTool(unittest.TestCase):
    def test_board_round_trip(self):
        rng = random.Random(17)
        gray4 = bytes(rng.randrange(16) for _ in range(WIDTH * HEIGHT))
        restored = board_to_gray(logical_to_board(gray4))
        self.assertEqual(restored, bytes(v * 17 for v in gray4))

    def test_png_is_valid_grayscale(self):
        gray = bytes((x * 7) & 255 for x in range(WIDTH * HEIGHT))
        with tempfile.TemporaryDirectory() as folder:
            path = pathlib.Path(folder) / 'a.png'
            write_png(path, gray)
            data = path.read_bytes()
        self.assertEqual(data[:8], b'\x89PNG\r\n\x1a\n')
        self.assertEqual(data[12:16], b'IHDR')
        width, height = int.from_bytes(data[16:20], 'big'), int.from_bytes(data[20:24], 'big')
        self.assertEqual((width, height, data[24], data[25]), (WIDTH, HEIGHT, 8, 0))
        idat = data.index(b'IDAT')
        length = int.from_bytes(data[idat - 4:idat], 'big')
        raw = zlib.decompress(data[idat + 4:idat + 4 + length])
        self.assertEqual(raw[0], 0)
        self.assertEqual(raw[1:1 + WIDTH], gray[:WIDTH])


if __name__ == '__main__':
    unittest.main()
