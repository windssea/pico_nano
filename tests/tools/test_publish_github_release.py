"""测试 GitHub Release 自动发布辅助工具。 / Test the automated GitHub Release publisher helper."""
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import publish_github_release as pub  # noqa: E402
import sync_gitee_release as mirror  # noqa: E402


class PublishGitHubReleaseTests(unittest.TestCase):
    def _sample_bundle(self):
        return mirror.Bundle(
            tag="v9.9.9",
            source_commit="a" * 40,
            assets=(
                mirror.Asset("PicoNano-aaaaaaa-upgrade.zip", b"upg"),
                mirror.Asset("PicoNano-aaaaaaa-full.zip", b"full"),
                mirror.Asset("PicoNano-web-installer-aaaaaaa.zip", b"web"),
                mirror.Asset("PicoNano-aaaaaaa-ota.bin", b"ota-app"),
                mirror.Asset("PicoNano-ota-manifest.bin", b"manifest"),
                mirror.Asset("SHA256SUMS", b"sums"),
            ),
        )

    def test_stage_assets_orders_immutable_before_ota_manifest(self):
        bundle = self._sample_bundle()
        with tempfile.TemporaryDirectory() as tmp:
            paths = pub.stage_assets(bundle, pathlib.Path(tmp))
            names = [p.name for p in paths]
            self.assertLess(names.index("PicoNano-aaaaaaa-ota.bin"), names.index("PicoNano-ota-manifest.bin"))
            self.assertLess(names.index("PicoNano-ota-manifest.bin"), names.index("SHA256SUMS"))
            self.assertEqual(set(names), {a.name for a in bundle.assets})

    def test_extract_release_notes_reads_matching_section(self):
        bundle = pub.publish_github_release(ROOT, "v1.1.1", "windssea/pico_nano", apply=False)
        notes = pub.extract_release_notes(ROOT, bundle)
        self.assertIn("# PicoNano 1.1.1", notes)
        self.assertIn(bundle.source_commit, notes)
        self.assertIn("三国华容道", notes)
        self.assertIn("PicoNano-ota-manifest.bin", notes)

    def test_diagnostic_tag_is_rejected(self):
        with self.assertRaises(ValueError):
            pub.publish_github_release(ROOT, "v1.0.6-build-3cb2d8a", "windssea/pico_nano", apply=False)

    def test_new_release_creates_draft_uploads_and_publishes(self):
        bundle = self._sample_bundle()
        calls = []

        def fake_gh(args, check=True):
            calls.append(list(args))
            if args[:2] == ["release", "view"]:
                return subprocess.CompletedProcess(["gh"] + list(args), 1, "", "release not found")
            return subprocess.CompletedProcess(["gh"] + list(args), 0, "", "")

        with mock.patch.object(mirror, "read_bundle", return_value=bundle), \
             mock.patch.object(pub, "run_gh", side_effect=fake_gh), \
             mock.patch.object(mirror, "github_release", return_value={"tag_name": "v9.9.9"}) as verify:
            pub.publish_github_release(ROOT, "v9.9.9", "windssea/pico_nano", apply=True)

        self.assertEqual(calls[0][:3], ["release", "view", "v9.9.9"])
        self.assertEqual(calls[1][:3], ["release", "create", "v9.9.9"])
        self.assertIn("--draft", calls[1])
        uploads = [pathlib.Path(c[3]).name for c in calls if c[:2] == ["release", "upload"]]
        self.assertEqual(len(uploads), len(bundle.assets))
        self.assertLess(uploads.index("PicoNano-aaaaaaa-ota.bin"), uploads.index("PicoNano-ota-manifest.bin"))
        self.assertEqual(calls[-1], ["release", "edit", "v9.9.9", "--repo", "windssea/pico_nano", "--draft=false"])
        verify.assert_called_once_with(bundle, "windssea/pico_nano", wait_seconds=60)

    def test_existing_release_uploads_only_missing_and_rejects_size_conflict(self):
        bundle = self._sample_bundle()
        # 缺失两个附件时只补传缺失项。 / Upload only missing assets when release already exists.
        existing = {
            "isDraft": False,
            "tagName": "v9.9.9",
            "assets": [
                {"name": a.name, "size": len(a.content)}
                for a in bundle.assets
                if a.name not in ("PicoNano-ota-manifest.bin", "SHA256SUMS")
            ],
        }
        calls = []

        def fake_gh(args, check=True):
            calls.append(list(args))
            if args[:2] == ["release", "view"]:
                return subprocess.CompletedProcess(["gh"] + list(args), 0, json.dumps(existing), "")
            return subprocess.CompletedProcess(["gh"] + list(args), 0, "", "")

        with mock.patch.object(mirror, "read_bundle", return_value=bundle), \
             mock.patch.object(pub, "run_gh", side_effect=fake_gh), \
             mock.patch.object(mirror, "github_release", return_value={"tag_name": "v9.9.9"}):
            pub.publish_github_release(ROOT, "v9.9.9", "windssea/pico_nano", apply=True)

        uploads = [pathlib.Path(c[3]).name for c in calls if c[:2] == ["release", "upload"]]
        self.assertEqual(uploads, ["PicoNano-ota-manifest.bin", "SHA256SUMS"])
        self.assertFalse(any(c[:2] == ["release", "edit"] for c in calls))

        # 远端同名附件大小冲突时必须拒绝覆盖。 / Refuse to overwrite when remote asset size conflicts.
        bad = dict(existing)
        bad["assets"] = [{"name": "PicoNano-aaaaaaa-ota.bin", "size": 999999}]

        def fake_gh_conflict(args, check=True):
            return subprocess.CompletedProcess(["gh"] + list(args), 0, json.dumps(bad), "")

        with mock.patch.object(mirror, "read_bundle", return_value=bundle), \
             mock.patch.object(pub, "run_gh", side_effect=fake_gh_conflict):
            with self.assertRaises(ValueError):
                pub.publish_github_release(ROOT, "v9.9.9", "windssea/pico_nano", apply=True)


if __name__ == "__main__":
    unittest.main()
