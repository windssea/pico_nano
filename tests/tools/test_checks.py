"""检查分区和文档验证器。 / Check partition and documentation validation."""
import pathlib
import sys
import tempfile
import unittest
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "tools"))
from check_docs import check_markdown, check_partitions


class Checks(unittest.TestCase):
    def test_partition_rejects_overlap_and_flash_overflow(self):
        check_partitions("ota_0,app,ota_0,0x20000,0x600000\ndata,data,nvs,0x620000,0x1000\n")
        for content in ["a,data,nvs,0x9000,0x2000\nb,data,nvs,0xa000,0x1000\n",
                        "a,data,nvs,0xf00000,0x200000\n", "a,app,ota_0,0x21000,0x1000\n",
                        "a,data,nvs,0x1000,0x1000\n", "a,data,nvs,0x9000,0x0\n"]:
            with self.assertRaises(ValueError):
                check_partitions(content)

    def test_markdown_requires_local_targets_and_closed_fences(self):
        with tempfile.TemporaryDirectory() as folder:
            root = pathlib.Path(folder)
            doc = root / "entry.md"
            (root / "valid.md").write_text("# valid", encoding="utf-8")
            doc.write_text("[valid](valid.md) [external](https://example.test)\n", encoding="utf-8")
            self.assertEqual(check_markdown(root), [])
            doc.write_text("[broken](absent.md)\n```c\n", encoding="utf-8")
            errors = check_markdown(root)
            self.assertEqual(len(errors), 2)


if __name__ == "__main__":
    unittest.main()
