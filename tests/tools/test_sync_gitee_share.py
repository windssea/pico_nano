"""测试共享素材与词典库 Gitee 自动同步工具（不联网、纯本地模拟 API）。
Test shared-assets & dictionary Gitee mirror script offline with a mock API."""
import hashlib
import json
import pathlib
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import sync_gitee_share


class FakeGiteeAPI:
    def __init__(self):
        self.releases = {}
        self.attachments = {}
        self.next_id = 1
        self.calls = []

    def request(self, path, method="GET", payload=None, attachment=None):
        self.calls.append((method, path, attachment.name if attachment else None))
        if path == "/repos/windssea/pico_nano" and method == "GET":
            return {"private": False, "default_branch": "main"}
        if path.startswith("/repos/windssea/pico_nano/releases/tags/") and method == "GET":
            tag = path.rsplit("/", 1)[-1]
            return self.releases.get(tag)
        if path == "/repos/windssea/pico_nano/releases" and method == "POST":
            rel = {"id": 99, "tag_name": payload["tag_name"], "body": payload["body"]}
            self.releases[payload["tag_name"]] = rel
            self.attachments[99] = []
            return rel
        if path.startswith("/repos/windssea/pico_nano/releases/99/attach_files"):
            if method == "GET":
                return list(self.attachments.get(99, []))
            if method == "POST" and attachment is not None:
                aid = str(self.next_id)
                self.next_id += 1
                entry = {
                    "id": aid,
                    "name": attachment.name,
                    "browser_download_url": f"https://gitee.com/mock/{aid}/{attachment.name}",
                    "_content": attachment.content,
                }
                self.attachments.setdefault(99, []).append(entry)
                return entry
            if method == "DELETE":
                aid = path.rsplit("/", 1)[-1]
                self.attachments[99] = [a for a in self.attachments.get(99, []) if a["id"] != aid]
                return None
        if path == "/repos/windssea/pico_nano/releases/99" and method == "PATCH":
            self.releases["share-assets"]["body"] = payload["body"]
            return self.releases["share-assets"]
        raise AssertionError(f"Unexpected request: {method} {path}")


class ShareGiteeSyncTest(unittest.TestCase):
    def test_build_bundle_and_idempotent_sync_with_replacement(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            mask_dir = root / "assets/share/image/mask"
            mask_dir.mkdir(parents=True)
            sample = next((ROOT / "assets/share/image/mask").glob("*.png"))
            (mask_dir / sample.name).write_bytes(sample.read_bytes())

            manifest_doc, assets = sync_gitee_share.build_share_bundle(root)
            names = [a.name for a in assets]
            self.assertIn("PicoNano-share-mask.zip", names)
            self.assertIn("PicoNano.zip", names)
            self.assertIn("share-manifest.json", names)
            self.assertIn("SHA256SUMS", names)

            api = FakeGiteeAPI()

            def fake_verify(asset, remote):
                if remote["_content"] != asset.content or hashlib.sha256(remote["_content"]).hexdigest() != asset.sha256:
                    raise ValueError("mismatch")

            with patch.object(sync_gitee_share, "verify_asset", side_effect=fake_verify):
                first = sync_gitee_share.sync_share(manifest_doc, assets, "windssea/pico_nano", "windssea/pico_nano", "share-assets", api)
                self.assertEqual(set(first["uploaded"]), set(names))
                self.assertEqual(first["replaced"], [])

                # Re-running with identical files uploads nothing.
                second = sync_gitee_share.sync_share(manifest_doc, assets, "windssea/pico_nano", "windssea/pico_nano", "share-assets", api)
                self.assertEqual(second["uploaded"], [])
                self.assertEqual(second["replaced"], [])
                self.assertEqual(set(second["unchanged"]), set(names))

                # Adding a font triggers upload of new font pack + replacement of manifest & SHA256SUMS.
                font_dir = root / "assets/share/font"
                font_dir.mkdir(parents=True)
                (font_dir / "DemoFont.ttf").write_bytes(b"\x00\x01\x00\x00demo-font")
                manifest2, assets2 = sync_gitee_share.build_share_bundle(root)
                third = sync_gitee_share.sync_share(manifest2, assets2, "windssea/pico_nano", "windssea/pico_nano", "share-assets", api)
                self.assertIn("PicoNano-share-font.zip", third["uploaded"])
                self.assertIn("share-manifest.json", third["replaced"])
                self.assertIn("SHA256SUMS", third["replaced"])
                self.assertIn("PicoNano-share-mask.zip", third["unchanged"])
                parsed_manifest = json.loads(next(a.content for a in assets2 if a.name == "share-manifest.json").decode("utf-8"))
                font_cat = next(c for c in parsed_manifest["categories"] if c["id"] == "font")
                self.assertEqual(font_cat["count"], 1)


if __name__ == "__main__":
    unittest.main()
