"""生成网页安装器静态站点：复制 installer/ 与 releases/ 中的固件并写入清单。
Build the web-installer static site: copy installer/ and the releases/ firmware, then write the manifest."""
import argparse
import functools
import hashlib
import http.server
import json
import pathlib
import re
import shutil

ROOT = pathlib.Path(__file__).resolve().parents[1]
# 清单键 → 发布目录内文件名。/ Manifest key -> file name inside a release folder.
FILES = {
    "bootloader": "bootloader.bin",
    "partitionTable": "partition-table.bin",
    "otaData": "ota_data_initial.bin",
    "app": "pico_nano.bin",
    "factoryData": "first-install/factory-data.bin",
    "factoryWallpaper": "first-install/factory-wallpaper.bin",
}
# flash_args 中必须出现的固定偏移。/ Fixed offsets that flash_args must list.
OFFSETS = {"bootloader.bin": 0x0, "partition-table.bin": 0x8000}
RELEASE = re.compile(r"pico_nano-(\d+(?:\.\d+)+)-esp32s3")
UPGRADE = re.compile(r"pico_nano-(\d+(?:\.\d+)+)-([0-9a-f]{7})-upgrade")
FULL = re.compile(r"pico_nano-(\d+(?:\.\d+)+)-([0-9a-f]{7})-full")


def read_full(folder):
    """校验同源码完整包的全部镜像，不借用历史引导文件。 / Validate every image in a full package without borrowing historical boot files."""
    match = FULL.fullmatch(folder.name)
    metadata = json.loads((folder / "manifest.json").read_text(encoding="utf-8"))
    if (metadata.get("mode") != "full" or metadata.get("target") != "esp32s3" or
            metadata.get("board") != "RDP-G01-W" or metadata.get("version") != match.group(1) or
            not metadata.get("source_commit", "").startswith(match.group(2))):
        raise SystemExit(f"{folder.name}: invalid full package identity")
    files = {}
    for key, name in FILES.items():
        data = (folder / name).read_bytes()
        entry = metadata["files"][name]
        digest = hashlib.sha256(data).hexdigest()
        if entry["sha256"] != digest or entry["bytes"] != len(data):
            raise SystemExit(f"{folder.name}/{name}: checksum mismatch")
        files[key] = {"name": name, "size": len(data), "sha256": digest}
    if (files["partitionTable"]["size"] != 0xc00 or not 0 < files["app"]["size"] <= 0x600000 or
            not 0 < files["bootloader"]["size"] <= 0x8000 or files["otaData"]["size"] != 0x2000 or
            any(files[k]["size"] != 0x100000 for k in ("factoryData", "factoryWallpaper"))):
        raise SystemExit(f"{folder.name}: invalid full image sizes")
    version = f"{match.group(1)}+{match.group(2)}"
    return {"version": version, "commit": metadata["source_commit"], "builtAt": metadata.get("built_at", ""),
            "mode": "full", "verifiedOnHardware": False, "path": f"firmware/{version}-full/", "files": files, "_folder": folder}


def read_upgrade(folder):
    """发布保留布局升级，只暴露应用与校验表。 / Publish a preserving upgrade with only the app and reference table."""
    match = UPGRADE.fullmatch(folder.name)
    metadata = json.loads((folder / "manifest.json").read_text(encoding="utf-8"))
    files = {}
    for key, name in (("app", "pico_nano.bin"), ("partitionTable", "expected-partition-table.bin")):
        data = (folder / name).read_bytes()
        entry = metadata["files"][name]
        digest = hashlib.sha256(data).hexdigest()
        if entry["sha256"] != digest or entry["bytes"] != len(data):
            raise SystemExit(f"{folder.name}/{name}: checksum mismatch")
        files[key] = {"name": name, "size": len(data), "sha256": digest}
    if files["partitionTable"]["size"] != 0xc00 or not 0 < files["app"]["size"] <= 0x600000:
        raise SystemExit("Invalid preserving upgrade sizes")
    version = f"{match.group(1)}+{match.group(2)}"
    return {"version": version, "commit": metadata["source_commit"], "builtAt": metadata.get("built_at", ""), "mode": "app-only", "verifiedOnHardware": False,
            "path": f"firmware/{version}/", "files": files, "_folder": folder}


def version_key(version):
    return tuple(int(part) for part in version.split("."))


def read_release(folder):
    """校验一个发布目录并返回清单条目。/ Validate one release folder and return its manifest entry."""
    version = RELEASE.fullmatch(folder.name).group(1)
    sums = {}
    for line in (folder / "SHA256SUMS").read_text(encoding="utf-8").splitlines():
        digest, name = line.split(None, 1)
        sums[name.lstrip("*")] = digest
    files = {}
    for key, name in FILES.items():
        data = (folder / name).read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if sums.get(name) != digest:
            raise SystemExit(f"{folder.name}/{name}: SHA256SUMS mismatch")
        files[key] = {"name": name, "size": len(data), "sha256": digest}
    listed = {}
    for line in (folder / "flash_args").read_text(encoding="utf-8").splitlines()[1:]:
        offset, name = line.split()
        listed[name] = int(offset, 16)
    for name, offset in OFFSETS.items():
        if listed.get(name) != offset:
            raise SystemExit(f"{folder.name}: flash_args must place {name} at {offset:#x}")
    readme = (folder / "README.md").read_text(encoding="utf-8")
    commit = re.search(r"源码提交：([0-9a-f]{7,40})", readme)
    return {
        "version": version,
        "mode": "full",
        "legacy": True,
        "commit": commit.group(1) if commit else "",
        "verifiedOnHardware": False,
        "path": f"firmware/{version}/",
        "files": files,
        "_folder": folder,
    }


def build(out):
    releases = []
    for folder in (ROOT / "releases").iterdir():
        if not folder.is_dir():
            continue
        if RELEASE.fullmatch(folder.name):
            releases.append(read_release(folder))
        elif UPGRADE.fullmatch(folder.name):
            releases.append(read_upgrade(folder))
        elif FULL.fullmatch(folder.name):
            releases.append(read_full(folder))
    releases.sort(key=lambda r: (version_key(r["version"].split("+")[0]), r.get("mode") == "app-only", r.get("builtAt", "")), reverse=True)
    if not releases:
        raise SystemExit("releases/ has no pico_nano-<version>-esp32s3 folder")
    # 完整刷入后的设备也可只更新应用；额外白名单来自同源码同应用的已校验完整包。
    # Devices installed with a full package can update only the app; obtain the extra allowlist from a verified full package with identical source and app.
    for release in releases:
        if release.get("mode") != "app-only":
            continue
        matches = [other for other in releases if other.get("mode") == "full" and not other.get("legacy")
                   and other["version"] == release["version"] and other["commit"] == release["commit"]]
        if len(matches) > 1:
            raise SystemExit(f"{release['version']}: ambiguous full package")
        if not matches:
            continue
        full = matches[0]
        if any(release["files"]["app"][key] != full["files"]["app"][key] for key in ("size", "sha256")):
            raise SystemExit(f"{release['version']}: full/upgrade application mismatch")
        release["files"]["currentPartitionTable"] = {**full["files"]["partitionTable"], "name": "current-partition-table.bin"}
        release["_current_table"] = full["_folder"] / FILES["partitionTable"]
    if out.exists():
        shutil.rmtree(out)
    shutil.copytree(ROOT / "installer", out)
    for release in releases:
        target = out / release["path"]
        for key, entry in release["files"].items():
            (target / entry["name"]).parent.mkdir(parents=True, exist_ok=True)
            source = release["_current_table"] if key == "currentPartitionTable" else release["_folder"] / entry["name"]
            shutil.copyfile(source, target / entry["name"])
        del release["_folder"]
        release.pop("_current_table", None)
    (out / "firmware" / "manifest.json").write_text(json.dumps({"releases": releases}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (out / ".nojekyll").write_text("", encoding="utf-8")
    print(f"Installer site: {out} ({', '.join(r['version'] for r in releases)})")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=pathlib.Path, default=ROOT / "build-dev" / "installer-site")
    parser.add_argument("--serve", type=int, metavar="PORT", help="Serve on http://localhost:PORT after building")
    args = parser.parse_args()
    out = args.out.resolve()
    build(out)
    if args.serve:
        class Handler(http.server.SimpleHTTPRequestHandler):
            extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, ".js": "text/javascript"}
        handler = functools.partial(Handler, directory=str(out))
        print(f"Open http://localhost:{args.serve}/ in Chrome or Edge (Ctrl+C to stop)", flush=True)
        http.server.ThreadingHTTPServer(("127.0.0.1", args.serve), handler).serve_forever()


if __name__ == "__main__":
    main()
