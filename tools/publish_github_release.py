"""从已校验的 releases/ 目录自动发布 GitHub Release（仅校验与上传，不重建固件）。
Publish a GitHub Release from verified releases/ packages (verify and upload only; never rebuild firmware)."""
import argparse
import pathlib
import re
import subprocess
import sys
import tempfile

TOOLS_DIR = pathlib.Path(__file__).resolve().parent
ROOT = TOOLS_DIR.parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import sync_gitee_release as mirror  # noqa: E402


def extract_release_notes(root, bundle):
    """从 INSTALLATION.md 提取对应版本说明，并附带精确源码提交与摘要清单。
    Extract matching version notes from INSTALLATION.md with exact source commit and SHA-256 summary."""
    version = bundle.tag[1:]
    doc = root / "docs/INSTALLATION.md"
    section = ""
    if doc.exists():
        text = doc.read_text(encoding="utf-8")
        pattern = re.compile(
            rf"^## {re.escape(version)}(?:（[^）]*）|\s*\([^)]*\))?\s*$\n(.*?)(?=^## |\Z)",
            re.MULTILINE | re.DOTALL,
        )
        match = pattern.search(text)
        if match:
            section = match.group(1).strip()
    lines = [f"# PicoNano {version}", ""]
    if section:
        lines.extend([section, ""])
    lines.extend([
        f"- 固件精确源码：`{bundle.source_commit}`",
        "- 在线网页安装器：https://windssea.github.io/pico_nano/",
        "",
        "### SHA256SUMS",
        "```text",
    ])
    for asset in bundle.assets:
        if asset.name != "SHA256SUMS":
            lines.append(f"{asset.sha256}  {asset.name}")
    lines.append("```")
    return "\n".join(lines) + "\n"


def stage_assets(bundle, staging_dir):
    """将已校验附件落盘到临时目录；不可变镜像排在 OTA 清单之前。
    Stage verified assets to a temporary directory, ordering immutable images before the OTA manifest."""
    ordered = sorted(
        bundle.assets,
        key=lambda a: (1 if a.name == "PicoNano-ota-manifest.bin" else 2 if a.name == "SHA256SUMS" else 0, a.name),
    )
    paths = []
    for asset in ordered:
        path = staging_dir / asset.name
        path.write_bytes(asset.content)
        paths.append(path)
    return paths


def run_gh(args, check=True):
    """调用 gh CLI，不打印令牌或敏感环境变量。 / Invoke gh CLI without printing tokens or secrets."""
    cmd = ["gh"] + list(args)
    return subprocess.run(cmd, check=check, text=True, capture_output=True)


def publish_github_release(root, tag, repository, apply=False):
    """校验发布包并幂等创建/补齐 GitHub Release。 / Verify release bundle and idempotently create or complete the GitHub Release."""
    bundle = mirror.read_bundle(root, tag)
    notes = extract_release_notes(root, bundle)
    version = tag[1:]
    if not apply:
        print(f"[DRY-RUN] Verified {tag} ({bundle.source_commit}), {len(bundle.assets)} assets:")
        for asset in bundle.assets:
            print(f"  {asset.sha256}  {asset.name} ({len(asset.content)} B)")
        return bundle

    # 查询远端是否已有同名 Release。 / Query whether the GitHub Release already exists.
    existing = None
    view = run_gh(["release", "view", tag, "--repo", repository, "--json", "isDraft,tagName,assets"], check=False)
    if view.returncode == 0:
        import json
        existing = json.loads(view.stdout)

    with tempfile.TemporaryDirectory(prefix="piconano-gh-release-") as tmp:
        tmp_dir = pathlib.Path(tmp)
        asset_paths = stage_assets(bundle, tmp_dir)
        notes_file = tmp_dir / "NOTES.md"
        notes_file.write_text(notes, encoding="utf-8")

        if existing is None:
            # 先以草稿创建，逐一上传全部附件后再公开发布，避免半成品暴露。
            # Create as draft first, upload every verified asset, then publish so partial releases never appear.
            run_gh([
                "release", "create", tag,
                "--repo", repository,
                "--title", f"PicoNano {version}",
                "--notes-file", str(notes_file),
                "--draft",
            ])
            for path in asset_paths:
                print(f"Upload GitHub (draft): {path.name}", flush=True)
                run_gh(["release", "upload", tag, str(path), "--repo", repository, "--clobber"])
            run_gh(["release", "edit", tag, "--repo", repository, "--draft=false"])
            print(f"Published GitHub Release {tag} ({len(asset_paths)} assets)", flush=True)
        else:
            remote_assets = {a["name"]: a for a in existing.get("assets", [])}
            is_draft = bool(existing.get("isDraft"))
            for asset in bundle.assets:
                if asset.name in remote_assets:
                    remote = remote_assets[asset.name]
                    if remote.get("size") != len(asset.content):
                        raise ValueError(f"Conflicting remote GitHub asset size for {asset.name}; refusing overwrite")
            for path in asset_paths:
                if path.name not in remote_assets:
                    print(f"Upload GitHub (missing): {path.name}", flush=True)
                    run_gh(["release", "upload", tag, str(path), "--repo", repository])
            if is_draft:
                run_gh(["release", "edit", tag, "--repo", repository, "--draft=false"])
                print(f"Published existing draft GitHub Release {tag}", flush=True)
            else:
                print(f"Verified existing published GitHub Release {tag}", flush=True)

    # 复核公开发布结果（逐字节匿名下载核验，复用 sync_gitee_release.github_release）。
    # Re-verify the published release with anonymous byte-exact download checks via sync_gitee_release.github_release.
    mirror.github_release(bundle, repository, wait_seconds=60)
    print(f"GitHub Release {tag} verified end-to-end.", flush=True)
    return bundle


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True, help="Canonical release tag such as v1.1.1")
    parser.add_argument("--repo", default="windssea/pico_nano", help="GitHub owner/repo")
    parser.add_argument("--root", type=pathlib.Path, default=ROOT)
    parser.add_argument("--apply", action="store_true", help="Create/publish the GitHub Release (dry-run by default)")
    args = parser.parse_args()
    if not mirror.REPOSITORY.fullmatch(args.repo):
        raise SystemExit("Invalid GitHub repository format")
    try:
        publish_github_release(args.root, args.tag, args.repo, apply=args.apply)
    except (ValueError, subprocess.CalledProcessError) as exc:
        detail = exc.stderr.strip() if isinstance(exc, subprocess.CalledProcessError) and exc.stderr else str(exc)
        print(f"STOP: {detail}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
