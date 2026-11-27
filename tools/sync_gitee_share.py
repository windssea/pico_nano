"""打包并自动同步 PicoNano 共享素材（遮罩/壁纸/背景/横幅/封面/字体/书籍）与开源词典到 Gitee Release。
Package and mirror PicoNano shared assets (masks/wallpapers/backgrounds/banners/covers/fonts/books) and open-source dictionaries to a Gitee Release."""
import argparse
import json
import os
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))

import installer_site
from sync_gitee_release import API, Asset, REPOSITORY, verify_asset

TAG_PATTERN = re.compile(r"[A-Za-z0-9_.-]{1,64}")
MAX_ATTACHMENT_BYTES = 100_000_000


def build_share_bundle(root, gitee_repo=installer_site.GITEE_SHARE_REPO, tag=installer_site.GITEE_SHARE_TAG):
    """扫描 assets/share/ 与 assets/dicts/ 构建确定性 Gitee 同步附件集合。
    Scan assets/share/ and assets/dicts/ to build deterministic Gitee release attachments."""
    share_manifest, share_packs, compat_zip = installer_site.scan_share_assets(root, gitee_repo, tag)
    dictionaries = installer_site.read_dictionaries(root, gitee_repo, tag)

    assets = []
    for pack_name, pack_bytes in sorted(share_packs.items()):
        assets.append(Asset(pack_name, pack_bytes))
    if compat_zip is not None:
        assets.append(Asset("PicoNano.zip", compat_zip))
    for item in dictionaries:
        data = (root / "assets/dicts" / item["fileName"]).read_bytes()
        assets.append(Asset(item["fileName"], data))

    manifest_doc = {
        **share_manifest,
        "dictionaries": dictionaries,
    }
    manifest_bytes = (json.dumps(manifest_doc, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    assets.append(Asset("share-manifest.json", manifest_bytes))

    for asset in assets:
        if not re.fullmatch(r"[A-Za-z0-9_.-]+", asset.name):
            raise ValueError(f"Invalid share attachment name: {asset.name}")
        if not 0 < len(asset.content) < MAX_ATTACHMENT_BYTES:
            raise ValueError(f"Share attachment size out of Gitee bounds: {asset.name}")

    sums = "".join(f"{asset.sha256}  {asset.name}\n" for asset in assets).encode("utf-8")
    assets.append(Asset("SHA256SUMS", sums))
    return manifest_doc, tuple(assets)


def share_release_body(manifest_doc, github_repo):
    """生成 Gitee 共享素材 Release 的中文说明文本。 / Render the Gitee shared-assets release description."""
    lines = [
        "PicoNano 共享素材与开源词典国内高速下载镜像（随 GitHub 主分支自动同步）。",
        "",
        "## 共享素材分类包",
    ]
    for cat in manifest_doc.get("categories", []):
        pack = cat.get("pack")
        if pack:
            lines.append(f"- **{cat['title']}**（{cat['count']} 项）：`{pack['fileName']}` → 上传至{cat['uploadTarget']}")
        else:
            lines.append(f"- **{cat['title']}**：待补充（放入 `assets/share/{cat['dir']}/` 后自动同步）")
    dicts = manifest_doc.get("dictionaries", [])
    if dicts:
        lines.extend(["", "## 开源离线词典包（StarDict DictZip）"])
        for item in dicts:
            lines.append(f"- **{item['title']}**（{item.get('license', '')}）：`{item['fileName']}`")
    lines.extend([
        "",
        "## 校验与预览",
        "- 在线预览单个透明遮罩、背景与素材，请访问网页安装器「素材与词典」专区。",
        "- 所有附件完整摘要见 `SHA256SUMS` 与 `share-manifest.json`。",
        f"- 源码与素材贡献仓库：https://github.com/{github_repo}/tree/main/assets/share",
    ])
    return "\n".join(lines) + "\n"


def sync_share(manifest_doc, assets, repository, github_repo, tag, api, verify_only=False):
    """幂等同步共享素材 Release：摘要一致跳过，变更先删旧附件再上传并匿名校验读回。
    Idempotently sync shared assets: skip matching SHA-256, replace updated files, and anonymously verify downloads."""
    prefix = f"/repos/{repository}"
    repo = api.request(prefix)
    if not repo or repo.get("private") or not repo.get("default_branch"):
        raise ValueError("Gitee repository must exist, be public, and have an initial commit")
    release = api.request(f"{prefix}/releases/tags/{tag}")
    body = share_release_body(manifest_doc, github_repo)
    if not release:
        if verify_only:
            raise ValueError(f"Gitee release {tag} does not exist")
        release = api.request(f"{prefix}/releases", "POST", {
            "tag_name": tag,
            "target_commitish": repo["default_branch"],
            "name": "PicoNano 共享素材与词典库",
            "body": body,
            "prerelease": False,
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

    uploaded, replaced, unchanged = [], [], []
    for asset in assets:
        matches = [x for x in remote if x.get("name") == asset.name]
        if matches:
            up_to_date = False
            if len(matches) == 1:
                try:
                    verify_asset(asset, matches[0])
                    up_to_date = True
                except ValueError:
                    up_to_date = False
            if up_to_date:
                unchanged.append(asset.name)
                continue
            if verify_only:
                raise ValueError(f"Gitee share asset mismatch: {asset.name}")
            for old in matches:
                old_id = str(old.get("id", ""))
                if not re.fullmatch(r"[A-Za-z0-9_-]{1,128}", old_id):
                    raise ValueError(f"Invalid Gitee attachment ID for {asset.name}")
                api.request(f"{endpoint}/{old_id}", "DELETE")
            result = api.request(endpoint, "POST", attachment=asset)
            verify_asset(asset, result)
            replaced.append(asset.name)
        else:
            if verify_only:
                raise ValueError(f"Gitee share release is missing: {asset.name}")
            result = api.request(endpoint, "POST", attachment=asset)
            verify_asset(asset, result)
            uploaded.append(asset.name)

    if not verify_only and release.get("body") != body:
        api.request(f"{prefix}/releases/{release['id']}", "PATCH", {
            "name": "PicoNano 共享素材与词典库",
            "body": body,
        })

    return {
        "tag": tag,
        "url": f"https://gitee.com/{repository}/releases/tag/{tag}",
        "uploaded": uploaded,
        "replaced": replaced,
        "unchanged": unchanged,
        "assets": [{"name": a.name, "bytes": len(a.content), "sha256": a.sha256} for a in assets],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", default=installer_site.GITEE_SHARE_TAG, help="Rolling Gitee release tag for shared assets")
    parser.add_argument("--gitee-repo", default=installer_site.GITEE_SHARE_REPO)
    parser.add_argument("--github-repo", default="windssea/pico_nano")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--apply", action="store_true", help="Upload verified share packs to Gitee; requires GITEE_TOKEN")
    mode.add_argument("--verify-only", action="store_true", help="Verify public Gitee share downloads without writing")
    parser.add_argument("--report", type=pathlib.Path, help="Write a non-secret JSON verification report")
    args = parser.parse_args()
    try:
        if not TAG_PATTERN.fullmatch(args.tag):
            raise ValueError("Invalid share release tag")
        if any(not REPOSITORY.fullmatch(x) for x in (args.gitee_repo, args.github_repo)):
            raise ValueError("Repository must be owner/name")
        manifest_doc, assets = build_share_bundle(ROOT, args.gitee_repo, args.tag)
        report = {
            "tag": args.tag,
            "mode": "local-check",
            "url": f"https://gitee.com/{args.gitee_repo}/releases/tag/{args.tag}",
            "assets": [{"name": a.name, "bytes": len(a.content), "sha256": a.sha256} for a in assets],
        }
        if args.apply or args.verify_only:
            token = os.environ.get("GITEE_TOKEN") if args.apply else None
            if args.apply and not token:
                raise ValueError("Set GITEE_TOKEN in the environment; never pass tokens on the command line")
            api = API("https://gitee.com/api/v5", token)
            report = sync_share(manifest_doc, assets, args.gitee_repo, args.github_repo, args.tag, api, args.verify_only)
            report["mode"] = "verified"
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(report, ensure_ascii=False, indent=2))
        return 0
    except (ValueError, OSError, KeyError, TypeError) as exc:
        print(f"Gitee share sync stopped: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
