"""复跑样本时保留编辑与来源，不改私人原书。 / Preserve edits/provenance and private originals when regenerating samples."""
import hashlib
import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "tools"))
from make_sim_library import generate


class SimSamples(unittest.TestCase):
    def test_rerun_preserves_local_edits_and_private_original(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            original = root / "private.epub"
            original.write_bytes(b"private original")
            books = root / "library/books"
            local_font = root / "本机外置.TTF"
            local_font.write_bytes(b"private font copy test")
            books.parent.mkdir()
            (books.parent / "local-inputs.json").write_text(json.dumps({"files": [{"kind": "font", "path": str(local_font)}]}), encoding="utf-8")
            generate(books, original)
            digest = hashlib.sha256(original.read_bytes()).hexdigest()
            epub = books / "07-封面与插图.epub"
            epub_hash = hashlib.sha256(epub.read_bytes()).hexdigest()
            edited = books / "01-中文段落与标点.txt"
            edited.write_text("本机修改 / Local edit", encoding="utf-8")
            generate(books, original)
            self.assertEqual(edited.read_text(encoding="utf-8"), "本机修改 / Local edit")
            self.assertEqual(hashlib.sha256(original.read_bytes()).hexdigest(), digest)
            self.assertEqual(hashlib.sha256(epub.read_bytes()).hexdigest(), epub_hash)
            manifest = json.loads((root / "library/manifest.json").read_text(encoding="utf-8"))
            entries = {entry["path"]: entry for entry in manifest["files"]}
            self.assertEqual(entries["books/01-中文段落与标点.txt"]["license"], "not-declared")
            self.assertEqual(entries["wallpapers/01-透明几何.png"]["license"], "CC0-1.0")
            self.assertEqual(entries["fonts/ReadPicoSample.ttf"]["license"], "OFL-1.1")
            self.assertEqual(entries["books/private.epub"]["license"], "private-local-only")
            copied_font = books.parent / "fonts" / local_font.name
            self.assertEqual(copied_font.read_bytes(), local_font.read_bytes())
            self.assertEqual(entries["fonts/本机外置.TTF"]["license"], "private-local-only")
            copied_font.write_bytes(b"local edited copy")
            generate(books, original)
            self.assertEqual(copied_font.read_bytes(), b"local edited copy")
            self.assertEqual(local_font.read_bytes(), b"private font copy test")


if __name__ == "__main__":
    unittest.main()
