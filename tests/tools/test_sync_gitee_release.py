"""发布镜像的完整性、重试与鉴权边界。 / Release mirror integrity, retry, and authentication boundaries."""
import hashlib
import base64
import json
import pathlib
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "tools"))
import sync_gitee_release as mirror


class FakeAPI:
    """只模拟发布端点，不触及真实账号。 / Simulate release endpoints without touching real accounts."""
    def __init__(self, attachments=(), exists=True):
        self.attachments = list(attachments)
        self.exists = exists
        self.writes = []
        self.page_calls = []

    def request(self, path, method="GET", payload=None, attachment=None):
        if method == "POST":
            self.writes.append((path, payload, attachment))
            if attachment:
                result = {"name": attachment.name, "browser_download_url": f"https://gitee.com/files/{attachment.name}"}
                self.attachments.append(result)
                return result
            self.exists = True
            return {"id": 123}
        if path.endswith("/releases/tags/v0.8.0"):
            return {"id": 123} if self.exists else None
        if "/attach_files?" in path:
            self.page_calls.append(path)
            return self.attachments
        return {"private": False, "default_branch": "master"}


class MirrorTests(unittest.TestCase):
    def setUp(self):
        self.bundle = mirror.Bundle("v0.8.0", "a" * 40, (mirror.Asset("app.zip", b"package"), mirror.Asset("SHA256SUMS", b"sums")))
        self.github = {"html_url": "https://github.com/example/repo/releases/tag/v0.8.0", "prerelease": False}

    def verify(self, asset, remote):
        self.assertEqual(asset.name, remote["name"])

    def fixture(self, root):
        """以独立已知内容构造三包与清单。 / Build three packages and manifests from independent known bytes."""
        commit = "a" * 40
        for mode in ("upgrade", "full"):
            folder = root / "releases" / f"pico_nano-0.8.0-{commit[:7]}-{mode}"
            folder.mkdir(parents=True)
            app = b"firmware"
            (folder / "pico_nano.bin").write_bytes(app)
            info = {"brand": "PicoNano", "version": "0.8.0", "source_commit": commit,
                    "target": "esp32s3", "board": "RDP-G01-W", "mode": mode,
                    "files": {"pico_nano.bin": {"bytes": len(app), "sha256": hashlib.sha256(app).hexdigest()}}}
            (folder / "manifest.json").write_text(json.dumps(info), encoding="utf-8")
            names = [f"PicoNano-{commit[:7]}-{mode}.zip"]
            if mode == "upgrade":
                names.append(f"PicoNano-web-installer-{commit[:7]}.zip")
            checksums = []
            for name in names:
                data = name.encode()
                (folder / name).write_bytes(data)
                checksums.append(f"{hashlib.sha256(data).hexdigest()}  {name}\n")
            (folder / "PACKAGE_SHA256.txt").write_text("".join(checksums), encoding="utf-8")

    def test_packages_require_exact_checksums_and_same_application(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            self.fixture(root)
            bundle = mirror.read_bundle(root, "v0.8.0")
            self.assertEqual(len(bundle.assets), 4)
            self.assertEqual(bundle.assets[-1].name, "SHA256SUMS")
            app = next((root / "releases").glob("*-full")) / "pico_nano.bin"
            app.write_bytes(b"corrupt!")
            with self.assertRaisesRegex(ValueError, "Manifest digest mismatch"):
                mirror.read_bundle(root, "v0.8.0")

    def test_archive_corruption_and_ambiguous_sources_stop(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            self.fixture(root)
            upgrade = next((root / "releases").glob("*-upgrade"))
            archive = next(upgrade.glob("*-upgrade.zip"))
            original = archive.read_bytes()
            archive.write_bytes(b"changed")
            with self.assertRaisesRegex(ValueError, "Package digest mismatch"):
                mirror.read_bundle(root, "v0.8.0")
            archive.write_bytes(original)
            (root / "releases" / "pico_nano-0.8.0-bbbbbbb-upgrade").mkdir()
            with self.assertRaisesRegex(ValueError, "exactly one"):
                mirror.read_bundle(root, "v0.8.0")

    def test_full_and_upgrade_source_identity_must_match(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            self.fixture(root)
            full = next((root / "releases").glob("*-full"))
            info = json.loads((full / "manifest.json").read_text())
            info["source_commit"] = "a" * 39 + "b"
            (full / "manifest.json").write_text(json.dumps(info))
            with self.assertRaisesRegex(ValueError, "same source/application"):
                mirror.read_bundle(root, "v0.8.0")

    def test_diagnostic_tags_and_path_injection_rejected(self):
        for tag in ("v0.8.0-diag", "../v0.8.0", "v0.8.0;echo secret", "0.8.0"):
            with self.assertRaises(ValueError):
                mirror.read_bundle(pathlib.Path("."), tag)

    def test_missing_assets_are_uploaded_and_verified(self):
        api = FakeAPI(exists=False)
        with patch.object(mirror, "verify_asset", side_effect=self.verify) as verify:
            report = mirror.sync(self.bundle, "owner/repo", self.github, api)
        self.assertEqual(len(api.writes), 3)
        self.assertEqual(verify.call_count, 2)
        self.assertEqual(len(report["assets"]), 2)
        self.assertIn(self.bundle.source_commit, api.writes[0][1]["body"])
        self.assertEqual(api.writes[0][1]["target_commitish"], "master")

    def test_identical_retry_performs_no_writes(self):
        remote = [{"name": a.name} for a in self.bundle.assets]
        api = FakeAPI(remote)
        with patch.object(mirror, "verify_asset", side_effect=self.verify):
            mirror.sync(self.bundle, "owner/repo", self.github, api)
        self.assertEqual(api.writes, [])

    def test_conflicts_stop_before_uploading_missing_assets(self):
        api = FakeAPI([{"name": "SHA256SUMS"}])
        with patch.object(mirror, "verify_asset", side_effect=ValueError("digest mismatch")):
            with self.assertRaisesRegex(ValueError, "digest mismatch"):
                mirror.sync(self.bundle, "owner/repo", self.github, api)
        self.assertEqual(api.writes, [])

    def test_duplicate_remote_names_are_rejected(self):
        api = FakeAPI([{"name": "app.zip"}, {"name": "app.zip"}])
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            mirror.sync(self.bundle, "owner/repo", self.github, api)
        self.assertEqual(api.writes, [])

    def test_read_only_verification_never_creates_or_uploads(self):
        for api in (FakeAPI(exists=False), FakeAPI()):
            with self.assertRaises(ValueError):
                mirror.sync(self.bundle, "owner/repo", self.github, api, verify_only=True)
            self.assertEqual(api.writes, [])

    def test_github_draft_or_different_packages_stop(self):
        with patch.object(mirror.API, "request", return_value={"draft": True}):
            with self.assertRaisesRegex(ValueError, "already be published"):
                mirror.github_release(self.bundle, "owner/repo")
        release = {"tag_name": "v0.8.0", "assets": [{"name": "app.zip", "size": 1}]}
        with patch.object(mirror.API, "request", return_value=release):
            with self.assertRaisesRegex(ValueError, "size mismatch"):
                mirror.github_release(self.bundle, "owner/repo")

    def test_authentication_redirect_is_not_followed(self):
        redirect = mirror.NoRedirect()
        self.assertIsNone(redirect.redirect_request(None, None, 302, "", {}, "https://other.test"))
        with self.assertRaisesRegex(ValueError, "HTTPS"):
            mirror.HTTPSRedirect().redirect_request(None, None, 302, "", {}, "http://other.test")
        with self.assertRaisesRegex(ValueError, "without whitespace") as caught:
            mirror.API("https://gitee.com/api/v5", "private-token\n")
        self.assertNotIn("private-token", str(caught.exception))

    def test_asset_verification_requires_public_https_url(self):
        with self.assertRaisesRegex(ValueError, "no public download URL"):
            mirror.verify_asset(self.bundle.assets[0], {"name": "app.zip"})
        with self.assertRaisesRegex(ValueError, "HTTPS"):
            mirror.download({"url": "http://example.test/app.zip", "size": 1, "sha256": "a" * 64})

    def test_explicit_guide_initialization_reads_back_and_is_idempotent(self):
        saved, writes = {}, []

        def request(path, method="GET", payload=None):
            if method == "GET":
                return saved.get(path)
            writes.append(path)
            saved[path] = {"content": payload["content"], "sha": "new-sha"}

        api = FakeAPI()
        api.request = request
        self.assertEqual(mirror.initialize_guides(api, "owner/repo", "upstream/repo"), ["README.md", "README.en.md"])
        self.assertEqual(len(writes), 2)
        mirror.initialize_guides(api, "owner/repo", "upstream/repo")
        self.assertEqual(len(writes), 2)
        self.assertTrue(all(base64.b64decode(v["content"]).startswith(b"# PicoNano") for v in saved.values()))


if __name__ == "__main__":
    unittest.main()
