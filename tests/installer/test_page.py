"""安装页DOM契约，避免展示布局遮蔽安全控件。 / Installer DOM contracts prevent presentation layout from shadowing safety controls."""
import json
from collections import Counter
from html.parser import HTMLParser
from pathlib import Path
from shutil import copyfile, copytree
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch
from zipfile import ZipFile

from tools import installer_site

ROOT = Path(__file__).resolve().parents[2]


class Page(HTMLParser):
    """只解析静态标签和属性，不执行安装逻辑。 / Parse static tags and attributes without running installer logic."""

    def __init__(self, root=ROOT / "installer"):
        super().__init__()
        self.elements = []
        self.feed((root / "index.html").read_text(encoding="utf-8"))

    def handle_starttag(self, tag, attrs):
        self.elements.append((tag, dict(attrs)))


class InstallerPage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temporary = TemporaryDirectory()
        cls.addClassCleanup(temporary.cleanup)
        root = Path(temporary.name)
        for path in ("installer", "assets/wall", "assets/dicts", "releases/pico_nano-0.0.57-esp32s3"):
            copytree(ROOT / path, root / path)
        (root / "docs").mkdir(parents=True, exist_ok=True)
        for doc in ("docs/CHANGELOG.md", "docs/INSTALLATION.md"):
            copyfile(ROOT / doc, root / doc)
        cls.site = root / "site"
        with patch.object(installer_site, "ROOT", root):
            installer_site.build(cls.site, latest_only=True)

    def test_ids_are_unique_so_runtime_controls_cannot_be_shadowed(self):
        counts = Counter(attrs["id"] for _, attrs in Page().elements if "id" in attrs)
        self.assertEqual({key: count for key, count in counts.items() if count != 1}, {})

    def test_write_button_starts_disabled_and_clear_choices_start_unchecked(self):
        elements = Page().elements
        install = [(tag, attrs) for tag, attrs in elements if attrs.get("id") == "install"]
        self.assertEqual(len(install), 1)
        self.assertEqual(install[0][0], "button")
        self.assertIn("disabled", install[0][1])
        plan = next(attrs for _, attrs in elements if attrs.get("id") == "plan")
        self.assertIn("hidden", plan)
        for key in ("clear-nvs", "initialize-storage", "confirm-model", "confirm-full"):
            tag, attrs = next((tag, attrs) for tag, attrs in elements if attrs.get("id") == key)
            self.assertEqual((tag, attrs.get("type")), ("input", "checkbox"))
            self.assertNotIn("checked", attrs)

    def test_product_page_omits_engineering_badges_and_keeps_bookmark_gallery(self):
        text = (ROOT / "installer/index.html").read_text(encoding="utf-8")
        for phrase in ("未登记", "诊断固件", "hw-warning", "hw-confirmed"):
            self.assertNotIn(phrase, text)
        self.assertTrue(any(tag == "button" and attrs.get("data-image") == "bookmark-lock"
                            for tag, attrs in Page().elements))

    def test_offline_scripts_styles_and_previews_are_packaged_locally(self):
        for tag, attrs in Page(self.site).elements:
            for key in ("src", "href"):
                value = attrs.get(key, "")
                if not value or value.startswith(("#", "https://")):
                    continue
                with self.subTest(tag=tag, asset=value):
                    self.assertTrue((self.site / value).is_file())

    def test_default_mask_wallpapers_are_downloadable_from_the_built_site(self):
        downloads = [(attrs.get("href"), attrs["download"])
                     for tag, attrs in Page(self.site).elements if tag == "a" and "download" in attrs]
        self.assertIn(("assets/PicoNano.zip", "PicoNano.zip"), downloads)
        source = ROOT / "assets/wall"
        served_zip = self.site / "assets/PicoNano.zip"
        self.assertEqual(served_zip.read_bytes(), (source / "PicoNano.zip").read_bytes())
        wallpapers = {f"PicoNano/{path.name}": path.read_bytes() for path in (source / "PicoNano").glob("*.png")}
        self.assertTrue(wallpapers)
        with ZipFile(served_zip) as archive:
            self.assertEqual({entry.filename: archive.read(entry) for entry in archive.infolist()
                              if not entry.is_dir()}, wallpapers)

    def test_gitee_download_and_local_zip_import_controls_exist(self):
        elements = {attrs["id"]: (tag, attrs) for tag, attrs in Page(self.site).elements if "id" in attrs}
        for key in ("gitee-download", "gitee-release"):
            tag, attrs = elements[key]
            self.assertEqual(tag, "a")
            self.assertTrue(attrs.get("href", "").startswith("https://gitee.com/windssea/pico_nano/releases"))
            self.assertNotIn("download", attrs)
        btn_tag, btn_attrs = elements["import-package"]
        self.assertEqual(btn_tag, "button")
        self.assertIn("disabled", btn_attrs)
        inp_tag, inp_attrs = elements["package-file"]
        self.assertEqual((inp_tag, inp_attrs.get("type")), ("input", "file"))
        self.assertIn(".zip", inp_attrs.get("accept", ""))
        self.assertTrue((self.site / "package.js").is_file())

    def test_open_source_dictionaries_and_changelog_are_packaged_and_verified(self):
        downloads = [(attrs.get("href"), attrs["download"])
                     for tag, attrs in Page(self.site).elements if tag == "a" and "download" in attrs]
        manifest = json.loads((self.site / "firmware/manifest.json").read_text(encoding="utf-8"))
        dicts = manifest.get("dictionaries", [])
        self.assertEqual(len(dicts), 3)
        for item in dicts:
            self.assertIn((item["path"], item["fileName"]), downloads)
            served = self.site / item["path"]
            self.assertTrue(served.is_file())
            self.assertEqual(served.stat().st_size, item["size"])
            with ZipFile(served) as archive:
                names = [e.filename for e in archive.infolist() if not e.is_dir()]
                self.assertTrue(any(n.endswith(".ifo") for n in names), names)
                self.assertTrue(any(n.endswith(".idx") for n in names), names)
                self.assertTrue(any(n.endswith(".dict.dz") for n in names), names)
                self.assertTrue(any(n.startswith("LICENSE") for n in names), names)
        changelog = manifest.get("changelog", [])
        self.assertGreaterEqual(len(changelog), 3)
        brand = (ROOT / "components/pn_core/include/pn_brand.h").read_text(encoding="utf-8")
        current_ver = next(line.split('"')[1] for line in brand.splitlines() if line.startswith('#define PN_PRODUCT_VERSION "'))
        self.assertEqual(changelog[0]["version"], current_ver)
        self.assertTrue(changelog[0]["items"])
        self.assertLessEqual(len(changelog[0]["items"]), 6)
        self.assertFalse(any("test_shelf_view.c" in item for item in changelog[0]["items"]))
        for entry in changelog:
            self.assertRegex(entry["version"], r"^\d+(?:\.\d+)+$")
            for line in entry["items"]:
                self.assertNotIn("](../", line)
        ids = {attrs["id"] for _, attrs in Page(self.site).elements if "id" in attrs}
        for required_id in ("release-notes-box", "release-notes-list", "changelog-list", "resources"):
            self.assertIn(required_id, ids)


if __name__ == "__main__":
    unittest.main()
