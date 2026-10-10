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
RELEASE = re.compile(r"pico_nano-(\d+\.\d+\.\d+)-esp32s3")


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
        "commit": commit.group(1) if commit else "",
        "verifiedOnHardware": False,
        "path": f"firmware/{version}/",
        "files": files,
        "_folder": folder,
    }


def build(out):
    releases = sorted((read_release(p) for p in (ROOT / "releases").iterdir() if p.is_dir() and RELEASE.fullmatch(p.name)),
                      key=lambda r: version_key(r["version"]), reverse=True)
    if not releases:
        raise SystemExit("releases/ has no pico_nano-<version>-esp32s3 folder")
    if out.exists():
        shutil.rmtree(out)
    shutil.copytree(ROOT / "installer", out)
    for release in releases:
        target = out / release["path"]
        for entry in release["files"].values():
            (target / entry["name"]).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(release["_folder"] / entry["name"], target / entry["name"])
        del release["_folder"]
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
