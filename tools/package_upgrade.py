"""发布板级应用专用升级包。 / Publish a dedicated board application upgrade package."""
import argparse
import datetime
import hashlib
import json
import pathlib
import re
import subprocess
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


def installation_readme(commit):
    """独立升级包将仓库相对链接指向固定源码。 / Point repository links in the portable upgrade README to pinned source."""
    source = ROOT / "docs/INSTALLATION.md"
    def replace(match):
        link = match.group(1)
        if "://" in link or link.startswith("#"):
            return match.group(0)
        path, separator, anchor = link.partition("#")
        relative = (source.parent / path).resolve().relative_to(ROOT).as_posix()
        return f"](https://github.com/windssea/pico_nano/blob/{commit}/{relative}{separator}{anchor})"
    return re.sub(r"\]\(([^)]+)\)", replace, source.read_text(encoding="utf-8")).encode("utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=pathlib.Path, required=True, help="Verified existing device partition table, 3072 bytes")
    args = parser.parse_args()
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    dirty = subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=no"], cwd=ROOT, text=True)
    if dirty:
        raise SystemExit("Commit tracked source changes before packaging")
    application = (ROOT / "build-board/pico_nano.bin").read_bytes()
    table = args.reference.read_bytes()
    config = (ROOT / "build-board/sdkconfig").read_text(encoding="utf-8")
    if len(table) != 0xc00 or not 0 < len(application) <= 0x600000:
        raise SystemExit("Invalid table or application size")
    if "CONFIG_SPIRAM_SPEED_120M=y" not in config or "CONFIG_ESP_PHY_ENABLE_USB=y" in config or "CONFIG_PN_PROFILE=y" in config:
        raise SystemExit("Expected board 120 MHz configuration with USB/radio exclusion")
    brand = (ROOT / "components/pn_core/include/pn_brand.h").read_text(encoding="utf-8")
    match = re.search(r'^#define PN_PRODUCT_VERSION "([0-9.]+)"', brand, re.MULTILINE)
    if not match:
        raise SystemExit("PN_PRODUCT_VERSION missing")
    version = match.group(1)
    folder = ROOT / "releases" / f"pico_nano-{version}-{commit[:7]}-upgrade"
    folder.mkdir(exist_ok=False)
    payload = {"pico_nano.bin": application, "expected-partition-table.bin": table,
               "flash_upgrade.py": (ROOT / "tools/flash_upgrade.py").read_text(encoding="utf-8").encode(),
               "README.md": installation_readme(commit),
               "requirements.txt": b"esptool==5.3.1\n"}
    metadata = {"brand": "PicoNano", "source_commit": commit, "version": version, "idf": "v6.1", "target": "esp32s3",
                "board": "RDP-G01-W", "built_at": datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).isoformat(),
                "config": "board defaults; USB/radio exclusion; CONFIG_PN_PROFILE disabled", "application_offset": "0x20000",
                "slot_bytes": 0x600000, "write_files": ["pico_nano.bin"], "write_executed": False, "hardware_acceptance": False,
                "files": {name: {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()} for name, data in payload.items()}}
    payload["manifest.json"] = (json.dumps(metadata, ensure_ascii=False, indent=2) + "\n").encode()
    payload["SHA256SUMS"] = ("\n".join(hashlib.sha256(data).hexdigest() + "  " + name for name, data in sorted(payload.items())) + "\n").encode()
    for name, data in payload.items():
        (folder / name).write_bytes(data)
    archive = folder / f"PicoNano-{commit[:7]}-upgrade.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zipped:
        for name, data in payload.items():
            zipped.writestr(name, data)
    (folder / "PACKAGE_SHA256.txt").write_bytes((hashlib.sha256(archive.read_bytes()).hexdigest() + "  " + archive.name + "\n").encode())
    print(folder)
    print("Build the web installer with python tools/dev.py installer, then commit the release folder.")


if __name__ == "__main__":
    main()
