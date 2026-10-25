"""发布镜像的完整性、重试与鉴权边界。 / Release mirror integrity, retry, and authentication boundaries."""
import hashlib
import base64
import json
import pathlib
import sys
import tempfile
import unittest
import struct
import zipfile
import subprocess
from unittest.mock import patch, MagicMock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "tools"))
import sync_gitee_release as mirror


class FakeAPI:
    """只模拟发布端点，不触及真实账号。 / Simulate release endpoints without touching real accounts."""
    def __init__(self, attachments=(), exists=True):
        self.attachments = list(attachments)
        self.exists = exists
        self.writes = []
        self.page_calls = []
        self.body = ""

    def request(self, path, method="GET", payload=None, attachment=None):
        if method == "DELETE":
            self.writes.append((path, payload, attachment))
            self.attachments = [x for x in self.attachments if str(x.get("id")) != path.rsplit("/",1)[1]]
            return None
        if method == "PATCH":
            self.writes.append((path, payload, attachment))
            self.body = payload.get("body",self.body)
            return {"id":123}
        if method == "POST":
            self.writes.append((path, payload, attachment))
            if attachment:
                result = {"id":1000+len(self.writes),"name": attachment.name, "browser_download_url": f"https://gitee.com/files/{attachment.name}", "data":attachment.content}
                self.attachments.append(result)
                return result
            self.exists = True
            return {"id": 123}
        if path.endswith("/releases/tags/v0.8.0"):
            return {"id": 123,"body":self.body} if self.exists else None
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
            app = bytearray(1024)
            app[0], app[1], app[12] = 0xe9, 3, 9
            struct.pack_into("<I", app, 32, 0xabcd5432)
            app[48:53] = b"0.8.0"
            app[80:89] = b"pico_nano"
            app = bytes(app)
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
                with zipfile.ZipFile(folder / name,"w",zipfile.ZIP_DEFLATED) as archive:
                    paths=([f"firmware/0.8.0+{commit[:7]}/pico_nano.bin",f"firmware/0.8.0+{commit[:7]}-full/pico_nano.bin"] if "web-installer" in name else ["pico_nano.bin"])
                    for path in paths:
                        archive.writestr(path,app)
                data = (folder/name).read_bytes()
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

    def test_archived_bundle_selection_is_exact_and_isolated(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=pathlib.Path(tmp);self.fixture(root);archive=root/'releases/archive';archive.mkdir()
            for folder in list((root/'releases').glob('pico_nano-*')):folder.rename(archive/folder.name)
            self.fixture(root)
            (next((root/'releases').glob('*-upgrade'))/'pico_nano.bin').write_bytes(b'corrupt current')
            self.assertEqual(mirror.read_bundle(root,'v0.8.0','a'*40).source_commit,'a'*40)
            with self.assertRaises(ValueError):mirror.read_bundle(root,'v0.8.0','a'*39+'b')
            with self.assertRaises(ValueError):mirror.read_bundle(root,'v0.8.0','../bad')

    def test_delete_accepts_empty_success_without_parsing_json(self):
        api=mirror.API('https://gitee.com/api/v5','private-token')
        response=MagicMock();response.status=204
        api.opener=MagicMock();api.opener.open.return_value.__enter__.return_value=response
        self.assertIsNone(api.request('/scoped-release/attachment','DELETE'))
        response.read.assert_not_called()

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

    def test_rebuild_preserves_known_stable_bytes_before_replacement(self):
        """同版本替换先归档并读回，旧不可变附件保持。 / Same-version replacement archives and reads back first, retaining immutable old attachments."""
        old = mirror.Asset("SHA256SUMS",b"old sums")
        backup = mirror.Asset("SHA256SUMS-1111111",old.content)
        api=FakeAPI([{"id":1,"name":old.name,"data":old.content},{"id":2,"name":"PicoNano-old-ota.bin","data":b"old image"}])
        def verify(asset,remote):
            if remote.get("name")!=asset.name or remote.get("data")!=asset.content:raise ValueError("digest mismatch")
        with patch.object(mirror,"verify_asset",side_effect=verify):
            mirror.sync(self.bundle,"owner/repo",self.github,api,replacements={old.name:(old,backup)})
            deleted=next(i for i,x in enumerate(api.writes) if x[0].endswith('/attach_files/1'))
            archived=next(i for i,x in enumerate(api.writes) if x[2] and x[2].name==backup.name)
            self.assertLess(archived,deleted)
            self.assertTrue(any(x['name']=="PicoNano-old-ota.bin" for x in api.attachments))
            self.assertTrue(any(x['name']==backup.name and x['data']==old.content for x in api.attachments))
            writes=len(api.writes)
            mirror.sync(self.bundle,"owner/repo",self.github,api,replacements={old.name:(old,backup)})
            self.assertEqual(len(api.writes),writes)

    def test_rebuild_unknown_conflict_and_read_only_never_write(self):
        old=mirror.Asset("SHA256SUMS",b"old sums");backup=mirror.Asset("SHA256SUMS-1111111",old.content)
        api=FakeAPI([{"id":1,"name":old.name,"data":b"unknown"}])
        with patch.object(mirror,"verify_asset",side_effect=ValueError("digest mismatch")):
            with self.assertRaises(ValueError):mirror.sync(self.bundle,"owner/repo",self.github,api,replacements={old.name:(old,backup)})
        self.assertEqual(api.writes,[])
        with self.assertRaises(ValueError):mirror.sync(self.bundle,"owner/repo",self.github,api,verify_only=True,replacements={old.name:(old,backup)})
        self.assertEqual(api.writes,[])

    def test_rebuild_failed_archive_verification_keeps_original(self):
        old=mirror.Asset("SHA256SUMS",b"old sums");backup=mirror.Asset("SHA256SUMS-1111111",old.content)
        api=FakeAPI([{"id":1,"name":old.name,"data":old.content}])
        def verify(asset,remote):
            if asset.name==backup.name or remote.get('data')!=asset.content:raise ValueError('digest mismatch')
        with patch.object(mirror,'verify_asset',side_effect=verify),self.assertRaises(ValueError):
            mirror.sync(self.bundle,"owner/repo",self.github,api,replacements={old.name:(old,backup)})
        self.assertFalse(any(x[0].endswith('/attach_files/1') for x in api.writes))
        self.assertTrue(any(x.get('id')==1 for x in api.attachments))

    def test_github_draft_or_different_packages_stop(self):
        with patch.object(mirror.API, "request", return_value={"draft": True}):
            with self.assertRaisesRegex(ValueError, "already be published"):
                mirror.github_release(self.bundle, "owner/repo")
        release = {"tag_name": "v0.8.0", "assets": [{"name": "app.zip", "size": 1}]}
        with patch.object(mirror.API, "request", return_value=release):
            with self.assertRaisesRegex(ValueError, "size mismatch"):
                mirror.github_release(self.bundle, "owner/repo")

    def test_github_published_before_uploads_gets_a_bounded_wait(self):
        incomplete = {"tag_name": "v0.8.0", "assets": []}
        complete = {"tag_name": "v0.8.0", "assets": [{"name": "app.zip", "size": 7, "browser_download_url": "https://example.test/app.zip"}]}
        with patch.object(mirror.API, "request", side_effect=[incomplete, complete]), \
                patch.object(mirror.time, "sleep") as sleep, patch.object(mirror, "download") as download:
            self.assertIs(mirror.github_release(self.bundle, "owner/repo", 20), complete)
            sleep.assert_called_once_with(10)
            download.assert_called_once()
        with patch.object(mirror.API, "request", return_value=incomplete) as request, \
                patch.object(mirror.time, "sleep") as sleep:
            with self.assertRaisesRegex(ValueError, "Missing"):
                mirror.github_release(self.bundle, "owner/repo", 20)
            self.assertEqual(request.call_count, 3)
            self.assertEqual(sleep.call_count, 2)
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

    def test_streaming_upload_keeps_credentials_off_arguments_and_never_replays(self):
        api = mirror.API("https://gitee.com/api/v5", "private-token")
        asset = mirror.Asset("firmware.zip", b"immutable zip bytes")
        def upload(command, **kwargs):
            self.assertNotIn("private-token", " ".join(command))
            self.assertIn("Authorization: Bearer private-token", kwargs["input"])
            self.assertNotIn("--location", command)
            seconds = 900 if len(asset.content)>4_000_000 else 300
            self.assertEqual(command[command.index("--max-time") + 1], str(seconds))
            self.assertEqual(kwargs["timeout"], seconds+30)
            source = pathlib.Path(command[command.index("--form") + 1].split(";", 1)[0][6:])
            self.assertEqual(source.read_bytes(), asset.content)
            pathlib.Path(command[command.index("--output") + 1]).write_text('{"name":"firmware.zip"}')
            return subprocess.CompletedProcess(command, 0, "201", "")
        with patch.object(mirror.shutil, "which", return_value="curl"), patch.object(mirror.subprocess, "run", side_effect=upload) as run:
            self.assertEqual(api.request("/upload", "POST", attachment=asset), {"name":"firmware.zip"})
            self.assertEqual(run.call_count, 1)
            asset = mirror.Asset("firmware.zip", b"x"*4_000_001)
            self.assertEqual(api.request("/upload", "POST", attachment=asset), {"name":"firmware.zip"})
            self.assertEqual(run.call_count, 2)
        failure = subprocess.CompletedProcess([], 28, "000", "private-token and server details")
        with patch.object(mirror.shutil, "which", return_value="curl"), patch.object(mirror.subprocess, "run", return_value=failure) as run:
            with self.assertRaisesRegex(ValueError, "check remote attachments") as caught:
                api.request("/upload", "POST", attachment=asset)
            self.assertNotIn("private-token", str(caught.exception))
            self.assertEqual(run.call_count, 1)

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
        for value in saved.values():
            text = base64.b64decode(value["content"]).decode("utf-8")
            self.assertIn("## 鸣谢 / Acknowledgements", text)
            for project in ("MindReset/read_pico_firmware", "wegooo-cell/read-pico-reader", "yiifanrecyclable/PicoReader-ifEdition",
                            "chinoryunqin/RickyOS", "CloudZao/Metalio-E-INK4", "446599/whiteos"):
                self.assertIn("https://github.com/" + project, text)
            self.assertIn("https://github.com/upstream/repo/blob/main/LICENSES/reference-manifest.json", text)
            self.assertNotIn("](LICENSES/", text)

    def test_optional_ota_requires_matching_image_and_real_signature(self):
        from cryptography.hazmat.primitives import serialization
        from cryptography.hazmat.primitives.asymmetric import ec
        from package_ota import make_manifest
        with tempfile.TemporaryDirectory() as tmp:
            root=pathlib.Path(tmp)
            self.fixture(root)
            upgrade=next((root/"releases").glob("*-upgrade"))
            ota=upgrade/"ota";ota.mkdir()
            key=ec.generate_private_key(ec.SECP256R1())
            image=(upgrade/"pico_nano.bin").read_bytes()
            envelope=make_manifest(image,b"\xe9"+b"\0"*99,"0.8.0","a"*40,key)
            (ota/"release-public.pem").write_bytes(key.public_key().public_bytes(serialization.Encoding.PEM,serialization.PublicFormat.SubjectPublicKeyInfo))
            def save(data):
                assets={"PicoNano-aaaaaaa-ota.bin":image,"PicoNano-ota-manifest.bin":data}
                for name,content in assets.items():(ota/name).write_bytes(content)
                (ota/"SHA256SUMS").write_text("".join(f"{hashlib.sha256(content).hexdigest()}  {name}\n" for name,content in assets.items()))
            save(envelope)
            self.assertEqual(len(mirror.read_bundle(root,"v0.8.0").assets),6)
            changed=bytearray(envelope);changed[450]^=1;save(bytes(changed))
            with self.assertRaisesRegex(ValueError,"signature verification"):
                mirror.read_bundle(root,"v0.8.0")

    def test_replacement_authority_requires_archived_bundle_and_same_signing_key(self):
        from cryptography.hazmat.primitives import serialization
        from cryptography.hazmat.primitives.asymmetric import ec
        from package_ota import make_manifest
        with tempfile.TemporaryDirectory() as tmp:
            root=pathlib.Path(tmp);self.fixture(root);upgrade=next((root/'releases').glob('*-upgrade'))
            ota=upgrade/'ota';ota.mkdir();key=ec.generate_private_key(ec.SECP256R1())
            image=(upgrade/'pico_nano.bin').read_bytes();boot=b'\xe9'+b'\0'*99
            old=make_manifest(image,boot,'0.8.0','a'*40,key)
            (ota/'release-public.pem').write_bytes(key.public_key().public_bytes(serialization.Encoding.PEM,serialization.PublicFormat.SubjectPublicKeyInfo))
            for name,data in {'PicoNano-aaaaaaa-ota.bin':image,'PicoNano-ota-manifest.bin':old}.items():(ota/name).write_bytes(data)
            (ota/'SHA256SUMS').write_text(''.join(f'{hashlib.sha256(data).hexdigest()}  {name}\n' for name,data in {'PicoNano-aaaaaaa-ota.bin':image,'PicoNano-ota-manifest.bin':old}.items()))
            previous=mirror.read_bundle(root,'v0.8.0');archive=root/'releases/archive';archive.mkdir()
            for folder in list((root/'releases').glob('pico_nano-*')):folder.rename(archive/folder.name)
            current=make_manifest(image,boot,'0.8.0','b'*40,key)
            bundle=mirror.Bundle('v0.8.0','b'*40,(mirror.Asset('PicoNano-ota-manifest.bin',current),mirror.Asset('SHA256SUMS',b'new sums')))
            authority=mirror.stable_replacements(root,bundle,'a'*40)
            self.assertEqual(authority['PicoNano-ota-manifest.bin'][0].content,old)
            self.assertEqual(authority['SHA256SUMS'][0].content,previous.assets[-1].content)
            wrong=make_manifest(image,boot,'0.8.0','b'*40,ec.generate_private_key(ec.SECP256R1()))
            with self.assertRaisesRegex(ValueError,'signing key'):
                mirror.stable_replacements(root,mirror.Bundle('v0.8.0','b'*40,(mirror.Asset('PicoNano-ota-manifest.bin',wrong),mirror.Asset('SHA256SUMS',b'new sums'))),'a'*40)
            with self.assertRaisesRegex(ValueError,'different previous'):
                mirror.stable_replacements(root,previous,'a'*40)


if __name__ == "__main__":
    unittest.main()
