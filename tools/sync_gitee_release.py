"""校验并同步既有发布包到 Gitee，不重建固件。 / Verify and mirror existing release packages to Gitee without rebuilding firmware."""
import argparse
import base64
import dataclasses
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time
import struct
import io
import zipfile
import urllib.error
import urllib.parse
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
REPOSITORY = re.compile(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+")
TAG = re.compile(r"v(\d+\.\d+\.\d+)")


@dataclasses.dataclass(frozen=True)
class Asset:
    """不可变发布附件。 / An immutable release attachment."""
    name: str
    content: bytes

    @property
    def sha256(self):
        """返回完整摘要。 / Return the complete digest."""
        return hashlib.sha256(self.content).hexdigest()


@dataclasses.dataclass(frozen=True)
class Bundle:
    """同一次源码构建的三种安装包。 / Three installation packages from the same source build."""
    tag: str
    source_commit: str
    assets: tuple


def read_bundle(root, tag):
    """仅接收唯一、同源且摘要完整的已发布包。 / Accept only unique, same-source packages with complete digests."""
    match = TAG.fullmatch(tag)
    if not match:
        raise ValueError("Expected a product release tag such as v0.8.0")
    version = match[1]
    folders = {}
    metadata = {}
    for mode in ("upgrade", "full"):
        candidates = sorted((root / "releases").glob(f"pico_nano-{version}-*-{mode}"))
        if len(candidates) != 1:
            raise ValueError(f"Expected exactly one {mode} package for {tag}")
        folder = candidates[0]
        info = json.loads((folder / "manifest.json").read_text(encoding="utf-8"))
        commit = info.get("source_commit", "")
        if (info.get("brand") != "PicoNano" or info.get("version") != version or
                info.get("target") != "esp32s3" or info.get("board") != "RDP-G01-W" or
                not re.fullmatch(r"[0-9a-f]{40}", commit) or
                folder.name != f"pico_nano-{version}-{commit[:7]}-{mode}" or
                (mode == "full" and info.get("mode") != "full")):
            raise ValueError(f"Invalid release identity: {folder.name}")
        for name, entry in info["files"].items():
            path = (folder / name).resolve()
            if not path.is_relative_to(folder.resolve()):
                raise ValueError("Manifest path escapes its package")
            data = path.read_bytes()
            if len(data) != entry["bytes"] or hashlib.sha256(data).hexdigest() != entry["sha256"]:
                raise ValueError(f"Manifest digest mismatch: {name}")
        folders[mode], metadata[mode] = folder, info
    if (metadata["upgrade"]["source_commit"] != metadata["full"]["source_commit"] or
            metadata["upgrade"]["files"]["pico_nano.bin"] != metadata["full"]["files"]["pico_nano.bin"]):
        raise ValueError("Upgrade and full packages must contain the same source/application")
    commit = metadata["upgrade"]["source_commit"]
    expected = {f"PicoNano-{commit[:7]}-upgrade.zip": folders["upgrade"],
                f"PicoNano-{commit[:7]}-full.zip": folders["full"],
                f"PicoNano-web-installer-{commit[:7]}.zip": folders["upgrade"]}
    checksums = {}
    for folder in folders.values():
        for line in (folder / "PACKAGE_SHA256.txt").read_text(encoding="utf-8").splitlines():
            row = re.fullmatch(r"([0-9a-f]{64})  ([A-Za-z0-9_.-]+\.zip)", line)
            if not row or row[2] in checksums:
                raise ValueError("Invalid or duplicate package checksum entry")
            checksums[row[2]] = row[1]
    if set(checksums) != set(expected):
        raise ValueError("Package checksum list must describe exactly the three release ZIPs")
    assets = []
    for name, folder in expected.items():
        asset = Asset(name, (folder / name).read_bytes())
        if asset.sha256 != checksums[name]:
            raise ValueError(f"Package digest mismatch: {name}")
        if not 0 < len(asset.content) < 100_000_000:
            raise ValueError(f"Package exceeds the Gitee attachment limit: {name}")
        app = (folders["upgrade"] / "pico_nano.bin").read_bytes()
        with zipfile.ZipFile(io.BytesIO(asset.content)) as archive:
            paths = [n for n in archive.namelist() if n.endswith("pico_nano.bin")]
            wanted = ([f"firmware/{version}+{commit[:7]}/pico_nano.bin", f"firmware/{version}+{commit[:7]}-full/pico_nano.bin"]
                      if name.startswith("PicoNano-web-installer-") else ["pico_nano.bin"])
            if sorted(paths) != sorted(wanted):
                raise ValueError("ZIP contains unexpected application entries")
            for path in paths:
                if archive.getinfo(path).file_size != len(app) or archive.read(path) != app:
                    raise ValueError("ZIP application differs from the package manifest")
        assets.append(asset)
    # OTA附件可选；旧版三ZIP不重打包，新版必须将签名清单与同一应用一起发布。
    # OTA assets are optional: never repack old ZIPs, and publish a signed manifest with the identical new app.
    ota = folders["upgrade"] / "ota"
    if ota.exists():
        from cryptography.hazmat.primitives import serialization
        from package_ota import verify_manifest
        names = [f"PicoNano-{commit[:7]}-ota.bin", "PicoNano-ota-manifest.bin"]
        rows = (ota / "SHA256SUMS").read_text(encoding="ascii").splitlines()
        if len(rows) != 2:
            raise ValueError("OTA checksum list must contain exactly the image and manifest")
        ota_sums = {}
        for row in rows:
            entry = re.fullmatch(r"([0-9a-f]{64})  ([A-Za-z0-9_.-]+)", row)
            if not entry or entry[2] in ota_sums:
                raise ValueError("Invalid or duplicate OTA checksum")
            ota_sums[entry[2]] = entry[1]
        if set(ota_sums) != set(names):
            raise ValueError("Unexpected OTA asset names")
        extra = [Asset(name, (ota / name).read_bytes()) for name in names]
        if any(asset.sha256 != ota_sums[asset.name] for asset in extra):
            raise ValueError("OTA attachment digest mismatch")
        if extra[0].content != (folders["upgrade"] / "pico_nano.bin").read_bytes():
            raise ValueError("OTA application differs from the published upgrade")
        public = serialization.load_pem_public_key((ota / "release-public.pem").read_bytes())
        try:
            payload = verify_manifest(extra[1].content, public)
        except Exception:
            raise ValueError("OTA manifest signature verification failed") from None
        expected_url = f"https://gitee.com/windssea/pico_nano/releases/download/{tag}/{names[0]}".encode("ascii")
        if (struct.unpack_from("<I", payload, 12)[0] != len(extra[0].content) or payload[72:112] != commit.encode("ascii") or
                payload[112:144] != hashlib.sha256(extra[0].content).digest() or
                payload[40:72].split(b"\0", 1)[0] != version.encode("ascii") or
                payload[176:432].split(b"\0", 1)[0] != expected_url):
            raise ValueError("OTA manifest source/version/image/URL mismatch")
        assets.extend(extra)
    sums = "".join(f"{a.sha256}  {a.name}\n" for a in assets).encode("utf-8")
    assets.append(Asset("SHA256SUMS", sums))
    return Bundle(tag, commit, tuple(assets))


class NoRedirect(urllib.request.HTTPRedirectHandler):
    """鉴权 API 不跟随重定向。 / Authenticated API calls never follow redirects."""
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


class HTTPSRedirect(urllib.request.HTTPRedirectHandler):
    """匿名下载仅跟随 HTTPS。 / Anonymous downloads follow HTTPS redirects only."""
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        if urllib.parse.urlsplit(newurl).scheme != "https":
            raise ValueError("Download redirect must use HTTPS")
        return super().redirect_request(req, fp, code, msg, headers, newurl)


class API:
    """限定来源的发布 API；令牌只留在进程中。 / Origin-bound release API; tokens remain in process memory."""
    def __init__(self, origin, token=None):
        if token and (not token.isascii() or any(ch.isspace() for ch in token)):
            raise ValueError("Release API token must be a single ASCII value without whitespace")
        self.origin, self.token = origin, token
        self.opener = urllib.request.build_opener(NoRedirect())

    def request(self, path, method="GET", payload=None, attachment=None):
        """只输出状态码，不回显请求或服务端错误正文。 / Report status codes without echoing requests or server error bodies."""
        if attachment is not None:
            if method != "POST" or payload is not None:
                raise ValueError("Attachment requests require a standalone POST")
            return self.upload(path, attachment)
        headers = {"Accept": "application/json", "User-Agent": "PicoNano-release-mirror"}
        if self.token:
            headers["Authorization"] = f"Bearer {self.token}"
        data = None
        if payload is not None:
            data = json.dumps(payload).encode("utf-8")
            headers["Content-Type"] = "application/json"
        try:
            req = urllib.request.Request(self.origin + path, data=data, headers=headers, method=method)
            with self.opener.open(req, timeout=120) as response:
                return json.load(response)
        except urllib.error.HTTPError as exc:
            if method == "GET" and exc.code == 404:
                return None
            raise ValueError(f"Release API {urllib.parse.urlsplit(self.origin).hostname} {method} failed (HTTP {exc.code})") from None
        except (urllib.error.URLError, TimeoutError, json.JSONDecodeError):
            raise ValueError("Release API connection or JSON response failed") from None

    def upload(self, path, attachment):
        """流式multipart上传；令牌只经stdin，超时不盲目重复POST。 / Stream multipart uploads; pass tokens only on stdin and never blindly replay timed-out POSTs."""
        executable = shutil.which("curl")
        if not executable:
            raise ValueError("Release uploads require curl")
        if not re.fullmatch(r"[A-Za-z0-9_.-]+", attachment.name):
            raise ValueError("Invalid attachment name")
        headers = 'header = "Accept: application/json"\n'
        if self.token:
            token = self.token.replace("\\", "\\\\").replace('"', '\\"')
            headers += f'header = "Authorization: Bearer {token}"\n'
        with tempfile.TemporaryDirectory(prefix="piconano-upload-") as folder:
            source = pathlib.Path(folder) / attachment.name
            response = pathlib.Path(folder) / "response.json"
            source.write_bytes(attachment.content)
            command = [executable, "--disable", "--config", "-", "--silent", "--show-error", "--proto", "=https",
                       "--connect-timeout", "20", "--max-time", "300", "--request", "POST",
                       "--user-agent", "PicoNano-release-mirror", "--output", str(response),
                       "--write-out", "%{http_code}", "--form",
                       f"file=@{source};filename={attachment.name};type=application/octet-stream",
                       "--url", self.origin + path]
            try:
                result = subprocess.run(command, input=headers, text=True, capture_output=True, timeout=330)
            except subprocess.TimeoutExpired:
                raise ValueError("Release upload timed out; check remote attachments before retrying") from None
            status = result.stdout.strip()
            if result.returncode or not re.fullmatch(r"2\d\d", status):
                code = status if re.fullmatch(r"\d{3}", status) else "unknown"
                raise ValueError(f"Release upload failed (curl exit {result.returncode}, HTTP {code}); check remote attachments before retrying")
            try:
                with response.open("rb") as stream:
                    data = stream.read(1024 * 1024 + 1)
                if len(data) > 1024 * 1024:
                    raise ValueError("Release upload response is too large")
                return json.loads(data)
            except (OSError, UnicodeError, json.JSONDecodeError):
                raise ValueError("Release upload returned invalid JSON; check remote attachments before retrying") from None


def download(asset):
    """不携带令牌读取附件，拒绝超长或不完整响应。 / Download attachments without a token; reject oversized or incomplete responses."""
    url = asset["url"]
    if urllib.parse.urlsplit(url).scheme != "https":
        raise ValueError("Attachment must have an HTTPS download URL")
    try:
        opener = urllib.request.build_opener(HTTPSRedirect())
        request = urllib.request.Request(url, headers={"User-Agent": "PicoNano-release-mirror"})
        with opener.open(request, timeout=120) as response:
            content = response.read(asset["size"] + 1)
    except (urllib.error.URLError, TimeoutError):
        raise ValueError("Anonymous attachment download failed") from None
    if len(content) != asset["size"]:
        raise ValueError("Attachment download size mismatch")
    if hashlib.sha256(content).hexdigest() != asset["sha256"]:
        raise ValueError("Attachment download digest mismatch")
    return content


def github_release(bundle, repository, wait_seconds=0):
    """同步前逐字节确认 GitHub 已公开发布相同包。 / Verify the same packages are publicly released on GitHub before mirroring."""
    api = API("https://api.github.com", os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN"))
    required = {a.name for a in bundle.assets if a.name != "SHA256SUMS"}
    for remaining in range(wait_seconds, -1, -10):
        info = api.request(f"/repos/{repository}/releases/tags/{bundle.tag}")
        if not info or info.get("draft") or info.get("tag_name") != bundle.tag:
            raise ValueError("GitHub release must already be published")
        ready = {a["name"] for a in info["assets"] if a.get("state", "uploaded") == "uploaded"}
        if required <= ready:
            break
        if remaining:
            # Release先发布后上传附件，给正常上传留有界等待。 / A release may be published before asset uploads finish; wait within a fixed bound.
            print("Waiting for GitHub release attachments", flush=True)
            time.sleep(10)
    if not required <= ready:
        raise ValueError("Missing or incomplete GitHub release attachments")
    for asset in bundle.assets:
        if asset.name == "SHA256SUMS":
            continue
        candidates = [x for x in info["assets"] if x["name"] == asset.name]
        if len(candidates) != 1:
            raise ValueError(f"Missing or duplicate GitHub asset: {asset.name}")
        remote = candidates[0]
        if remote["size"] != len(asset.content):
            raise ValueError(f"GitHub asset size mismatch: {asset.name}")
        print(f"Verify GitHub: {asset.name}", flush=True)
        download({"url": remote["browser_download_url"], "size": len(asset.content), "sha256": asset.sha256})
    return info


def release_body(bundle, github):
    """国内离线安装步骤和来源证据。 / Domestic offline installation steps and source evidence."""
    preview_note = "测试预览，尚未作为稳定版本交付；迁移方法和设备验收边界见原始发布说明。\n\n" if github.get("prerelease") else ""
    ota_note = ("设备 OTA 附件为 `PicoNano-*-ota.bin` 与 `PicoNano-ota-manifest.bin`；"
                "仅供已完成兼容引导迁移的设备使用，电脑安装仍下载 ZIP。\n\n"
                if any(a.name == "PicoNano-ota-manifest.bin" for a in bundle.assets) else "")
    return (f"PicoNano {bundle.tag[1:]} 国内安装包，与 GitHub 发布的三个 ZIP 逐字节一致。\n\n"
            f"{preview_note}"
            f"固件源码：`{bundle.source_commit}`\n\n"
            "- 推荐下载 `PicoNano-web-installer-*.zip`，解压后运行 `py serve.py`，"
            "在电脑 Chrome/Edge 打开 `http://localhost:8765/`，刷机过程中不再访问 GitHub。\n"
            "- `*-upgrade.zip` 是命令行保留布局升级包。\n"
            "- `*-full.zip` 是完整刷入包，请先备份并阅读安装说明，清除/初始化会丢失对应记录。\n"
            "- 下载后可用 `SHA256SUMS` 核对完整文件；不需要克隆 Git 仓库。\n\n"
            "三个 ZIP 供电脑 USB 串口安装；设备 Wi-Fi OTA 的支持状态以原始发布说明为准。\n\n"
            f"{ota_note}"
            f"[原始发布与说明]({github['html_url']})\n\n"
            "软件验证、固件构建与真机验收分别记录，以原始发布的验收边界为准。\n")


def sync(bundle, repository, github, api, verify_only=False):
    """幂等上传，冲突停止，完成后匿名下载核验。 / Upload idempotently, stop on conflicts, then verify anonymous downloads."""
    prefix = f"/repos/{repository}"
    repo = api.request(prefix)
    if not repo or repo.get("private") or not repo.get("default_branch"):
        raise ValueError("Gitee repository must exist, be public, and have an initial commit")
    release = api.request(f"{prefix}/releases/tags/{bundle.tag}")
    if not release:
        if verify_only:
            raise ValueError("Gitee release does not exist")
        release = api.request(f"{prefix}/releases", "POST", {
            "tag_name": bundle.tag, "target_commitish": repo["default_branch"],
            "name": f"PicoNano {bundle.tag[1:]}", "body": release_body(bundle, github),
            "prerelease": bool(github.get("prerelease")),
        })
    endpoint = f"{prefix}/releases/{release['id']}/attach_files"
    remote = []
    for page in range(1, 102):
        batch = api.request(f"{endpoint}?page={page}&per_page=100")
        if not isinstance(batch, list):
            raise ValueError("Invalid Gitee attachment list")
        remote.extend(batch)
        if len(batch) < 100:
            break
    else:
        raise ValueError("Gitee attachment pagination exceeded the bounded limit")
    report = {"tag": bundle.tag, "source_commit": bundle.source_commit,
              "url": f"https://gitee.com/{repository}/releases/tag/{bundle.tag}", "assets": []}
    # 先检查全部已存在附件，避免发现冲突之前上传其他包。 / Check all existing attachments before uploading any missing package.
    missing = []
    for asset in bundle.assets:
        matches = [x for x in remote if x["name"] == asset.name]
        if len(matches) > 1:
            raise ValueError(f"Duplicate Gitee attachment: {asset.name}")
        if matches:
            verify_asset(asset, matches[0])
        else:
            missing.append(asset)
    if verify_only and missing:
        raise ValueError("Gitee release is missing required attachments")
    for asset in missing:
        print(f"Upload Gitee: {asset.name}", flush=True)
        result = api.request(endpoint, "POST", attachment=asset)
        verify_asset(asset, result)
    for asset in bundle.assets:
        report["assets"].append({"name": asset.name, "bytes": len(asset.content), "sha256": asset.sha256})
    return report


def verify_asset(asset, remote):
    """以匿名下载字节确认附件，不能只信名称或大小。 / Verify anonymous download bytes, not just a name or size."""
    if remote.get("name") != asset.name:
        raise ValueError("Gitee attachment name mismatch")
    url = remote.get("browser_download_url")
    if not url:
        raise ValueError("Gitee attachment has no public download URL")
    print(f"Verify Gitee: {asset.name}", flush=True)
    download({"url": url, "size": len(asset.content), "sha256": asset.sha256})


def initialize_guides(api, repository, github_repository):
    """仅在显式首装请求时设置分发首页，并读回确认。 / Set distribution guides only on explicit initialization and verify readback."""
    releases = f"https://gitee.com/{repository}/releases"
    source = f"https://github.com/{github_repository}"
    texts = {
        "README.md": ("# PicoNano 国内下载\n\n"
                      f"[下载安装包]({releases})\n\n"
                      "推荐下载最新发行版中的 `PicoNano-web-installer-*.zip`。解压后运行 `py serve.py`，"
                      "在电脑 Chrome/Edge 打开 `http://localhost:8765/`，再用 USB 数据线连接阅读器。"
                      "刷机过程中不再访问 GitHub；无需克隆仓库，也不要直接双击 HTML。\n\n"
                      "- `*-upgrade.zip`：命令行保留布局升级包。\n"
                      "- `*-full.zip`：完整刷入包，先备份并阅读说明；清除/初始化会丢失对应记录。\n"
                      "- `SHA256SUMS`：三个 ZIP 的完整 SHA-256 校验值。\n\n"
                      "本仓库用于国内安装包分发，Gitee 标签标记分发版本。"
                      "请下载上述安装包；平台自动生成的 Source code ZIP 只包含分发说明，不能用于刷机。\n\n"
                      f"固件精确源码以包内 `manifest.json` 的 `source_commit` 为准，"
                      f"[源码、许可证与开发文档]({source})由原工程维护。"
                      "镜像与 GitHub 发布的安装包保持相同字节，不重新构建固件。\n\n"
                      "各版本功能及真机验收边界以发行说明为准，USB 串口安装与设备 Wi-Fi OTA 分别交付。\n"),
        "README.en.md": ("# PicoNano downloads\n\n"
                         f"[Download installation packages]({releases})\n\n"
                         "Download `PicoNano-web-installer-*.zip` from the latest release, extract it, "
                         "run `py serve.py`, and open `http://localhost:8765/` in desktop Chrome or Edge. "
                         "Connect the reader with a USB data cable. Installation needs no GitHub access or repository clone; "
                         "serve the files instead of opening the HTML file directly.\n\n"
                         "- `*-upgrade.zip`: command-line upgrade preserving the supported device layout.\n"
                         "- `*-full.zip`: full installation; back up and read the instructions before clearing or initializing data.\n"
                         "- `SHA256SUMS`: complete SHA-256 digests for all three ZIP packages.\n\n"
                         "This repository distributes installation packages. Its tags identify distribution versions. "
                         "The automatically generated Source code archives contain distribution guides, not firmware.\n\n"
                         f"The package manifest records the exact firmware `source_commit`. "
                         f"[Firmware source, licenses, and documentation]({source}) are maintained upstream. "
                         "Mirrored packages have identical bytes to the GitHub release; firmware is not rebuilt here.\n\n"
                         "Refer to each release for supported features and hardware acceptance. "
                         "USB serial installation and device Wi-Fi OTA are separate capabilities.\n"),
    }
    for name, text in texts.items():
        path = f"/repos/{repository}/contents/{name}"
        old = api.request(path)
        content = text.encode("utf-8")
        if old and base64.b64decode(old["content"]) == content:
            continue
        payload = {"content": base64.b64encode(content).decode("ascii"), "message": "docs: add PicoNano installation guide"}
        if old:
            payload["sha"] = old["sha"]
        api.request(path, "PUT" if old else "POST", payload)
        saved = api.request(path)
        if not saved or base64.b64decode(saved["content"]) != content:
            raise ValueError(f"Gitee guide readback mismatch: {name}")
    return list(texts)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True, help="Published GitHub release tag, e.g. v0.8.0")
    parser.add_argument("--gitee-repo", default="windssea/pico_nano")
    parser.add_argument("--github-repo", default="windssea/pico_nano")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--apply", action="store_true", help="Upload verified packages; requires GITEE_TOKEN")
    mode.add_argument("--verify-only", action="store_true", help="Verify existing public Gitee downloads without writing")
    parser.add_argument("--initialize-guides", action="store_true", help="With --apply, replace Gitee README.md/README.en.md with download guides once")
    parser.add_argument("--wait-for-assets", type=int, default=0, help="Wait 0-300 seconds in 10-second intervals for GitHub asset uploads")
    parser.add_argument("--report", type=pathlib.Path, help="Write a non-secret JSON verification report")
    args = parser.parse_args()
    try:
        if args.initialize_guides and not args.apply:
            raise ValueError("Guide initialization requires --apply")
        if not 0 <= args.wait_for_assets <= 300 or args.wait_for_assets % 10:
            raise ValueError("Asset wait must be 0-300 seconds in 10-second intervals")
        if any(not REPOSITORY.fullmatch(x) for x in (args.gitee_repo, args.github_repo)):
            raise ValueError("Repository must be owner/name")
        bundle = read_bundle(ROOT, args.tag)
        report = {"tag": bundle.tag, "source_commit": bundle.source_commit, "mode": "local-check",
                  "assets": [{"name": a.name, "bytes": len(a.content), "sha256": a.sha256} for a in bundle.assets]}
        if args.apply or args.verify_only:
            token = os.environ.get("GITEE_TOKEN") if args.apply else None
            if args.apply and not token:
                raise ValueError("Set GITEE_TOKEN in the environment; never pass tokens on the command line")
            github = github_release(bundle, args.github_repo, args.wait_for_assets)
            api = API("https://gitee.com/api/v5", token)
            report = sync(bundle, args.gitee_repo, github, api, args.verify_only)
            if args.initialize_guides:
                report["guides"] = initialize_guides(api, args.gitee_repo, args.github_repo)
            report["mode"] = "verified"
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(report, ensure_ascii=False, indent=2))
        return 0
    except (ValueError, OSError, KeyError, TypeError) as exc:
        print(f"Gitee mirror stopped: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
