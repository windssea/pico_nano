"""内置字体片段生成器的确定性、完整还原与坏来源拒绝。 / Font-view generation determinism, full reconstruction and invalid-source rejection."""
import contextlib
import importlib.util
import io
import pathlib
import struct
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('font_view', ROOT/'tools/embed_font_view.py')
view = importlib.util.module_from_spec(spec)
spec.loader.exec_module(view)


class FontView(unittest.TestCase):
    def setUp(self):
        self.source = ROOT/'assets/fonts/read-pico-ui.ttf'
        self.base = ROOT/'assets/fonts/read-pico-reading.ttf'

    def test_exact_and_deterministic(self):
        data, base = self.source.read_bytes(), self.base.read_bytes()
        local, spans = view.share(data, base)
        restored, begin = bytearray(), 0
        for end, offset in spans:
            bank = base if offset >> 31 else local
            start = offset & 0x7fffffff
            restored.extend(bank[start:start+end-begin]); begin = end
        self.assertEqual(bytes(restored), data)
        self.assertGreater(len(data)-len(local)-8*len(spans), 200*1024)
        self.assertEqual((local, spans), view.share(data, base))
        with tempfile.TemporaryDirectory() as tmp, contextlib.redirect_stdout(io.StringIO()):
            a, b = pathlib.Path(tmp)/'a.c', pathlib.Path(tmp)/'b.c'
            view.generate(self.source, self.base, a)
            view.generate(self.source, self.base, b)
            self.assertEqual(a.read_bytes(), b.read_bytes())

    def test_invalid_inputs(self):
        original = self.source.read_bytes()
        for size in (0, 8, 12, 100, len(original)-1):
            with self.assertRaises(ValueError):
                view.glyphs(original[:size])
        broken = bytearray(original); struct.pack_into('>I', broken, 20, len(original)+1)
        with self.assertRaises(ValueError):
            view.glyphs(broken)
        broken = bytearray(original); broken[28:32] = broken[12:16]
        with self.assertRaises(ValueError):
            view.glyphs(broken)
        with self.assertRaises(ValueError):
            view.share(b'not a font', self.base.read_bytes())


if __name__ == '__main__':
    unittest.main()
