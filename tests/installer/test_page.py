"""安装页DOM契约，避免展示布局遮蔽安全控件。 / Installer DOM contracts prevent presentation layout from shadowing safety controls."""
from collections import Counter
from html.parser import HTMLParser
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class Page(HTMLParser):
    """只解析静态标签和属性，不执行安装逻辑。 / Parse static tags and attributes without running installer logic."""

    def __init__(self):
        super().__init__()
        self.elements = []
        self.feed((ROOT / "installer/index.html").read_text(encoding="utf-8"))

    def handle_starttag(self, tag, attrs):
        self.elements.append((tag, dict(attrs)))


class InstallerPage(unittest.TestCase):
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
        for tag, attrs in Page().elements:
            for key in ("src", "href"):
                value = attrs.get(key, "")
                if not value or value.startswith(("#", "https://")):
                    continue
                with self.subTest(tag=tag, asset=value):
                    self.assertTrue((ROOT / "installer" / value).is_file())


if __name__ == "__main__":
    unittest.main()
