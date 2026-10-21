"""生成板级专用升级包或完整刷入包。 / Create a board app-only upgrade or full installation package."""
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
    parser.add_argument("--reference", type=pathlib.Path, help="Verified existing device partition table, 3072 bytes (app-only)")
    parser.add_argument("--full", action="store_true", help="Package board bootloader/table/OTA/app and empty storage images for full installation")
    args = parser.parse_args()
    if not args.full and not args.reference:
        parser.error("app-only packaging requires --reference")
    if args.full and args.reference:
        parser.error("full packaging uses the board build table, not --reference")
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    dirty = subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=no"], cwd=ROOT, text=True)
    if dirty:
        raise SystemExit("Commit tracked source changes before packaging")
    application = (ROOT / "build-board/pico_nano.bin").read_bytes()
    table = (ROOT / "build-board/partition_table/partition-table.bin").read_bytes() if args.full else args.reference.read_bytes()
    config = (ROOT / "build-board/sdkconfig").read_text(encoding="utf-8")
    if len(table) != 0xc00 or not 0 < len(application) <= 0x600000:
        raise SystemExit("Invalid table or application size")
    validate_build_config(config)
    brand = (ROOT / "components/pn_core/include/pn_brand.h").read_text(encoding="utf-8")
    match = re.search(r'^#define PN_PRODUCT_VERSION "([0-9.]+)"', brand, re.MULTILINE)
    if not match:
        raise SystemExit("PN_PRODUCT_VERSION missing")
    version = match.group(1)
    if args.full:
        description = json.loads((ROOT / "build-board/project_description.json").read_text(encoding="utf-8"))
        validate_build_version(application, version, description)
    kind = "full" if args.full else "upgrade"
    folder = ROOT / "releases" / f"pico_nano-{version}-{commit[:7]}-{kind}"
    payload = {"pico_nano.bin": application, "expected-partition-table.bin": table,
               "flash_upgrade.py": (ROOT / "tools/flash_upgrade.py").read_text(encoding="utf-8").encode(),
               "README.md": installation_readme(commit),
               "requirements.txt": b"esptool==5.3.1\n"}
    if args.full:
        payload = full_payload(ROOT / "build-board", ROOT / "build-dev", application, table)
        payload["README.md"] = installation_readme(commit)
    metadata = {"brand": "PicoNano", "source_commit": commit, "version": version, "idf": "v6.1", "target": "esp32s3",
                "board": "RDP-G01-W", "built_at": datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).isoformat(),
                "config": "board defaults; SDK USB/radio coexistence; TLS PSRAM; diagnostics disabled", "application_offset": "0x20000",
                "slot_bytes": 0x600000, "write_files": ["pico_nano.bin"], "write_executed": False, "hardware_acceptance": False,
                "files": {name: {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()} for name, data in payload.items()}}
    if args.full:
        metadata["mode"] = "full"
        metadata["write_files"] = ["bootloader.bin", "partition-table.bin", "ota_data_initial.bin", "pico_nano.bin"]
        metadata["storage_initialization"] = "blank complete partitions or explicit user choice only"
    folder.mkdir(exist_ok=False)
    payload["manifest.json"] = (json.dumps(metadata, ensure_ascii=False, indent=2) + "\n").encode()
    payload["SHA256SUMS"] = ("\n".join(hashlib.sha256(data).hexdigest() + "  " + name for name, data in sorted(payload.items())) + "\n").encode()
    for name, data in payload.items():
        (folder / name).parent.mkdir(parents=True, exist_ok=True)
        (folder / name).write_bytes(data)
    archive = folder / f"PicoNano-{commit[:7]}-{kind}.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zipped:
        for name, data in payload.items():
            zipped.writestr(name, data)
    (folder / "PACKAGE_SHA256.txt").write_bytes((hashlib.sha256(archive.read_bytes()).hexdigest() + "  " + archive.name + "\n").encode())
    print(folder)
    print("Build the web installer with python tools/dev.py installer, then commit the release folder.")


def validate_build_config(config):
    """拒绝旧无线/TLS策略、CI时钟及诊断包冒充正式板级包。 / Reject stale radio/TLS policy, CI clocks and diagnostics in product board packages."""
    options = dict(re.findall(r"^(CONFIG_[A-Z0-9_]+)=(.*)$", config, re.MULTILINE))
    required = ("CONFIG_SPIRAM_SPEED_120M", "CONFIG_ESP_PHY_ENABLE_USB", "CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC")
    forbidden = ("CONFIG_PN_PROFILE", "CONFIG_PN_TEST_CONSOLE")
    if any(options.get(key) != "y" for key in required) or any(options.get(key) == "y" for key in forbidden):
        raise ValueError("Expected product board 120 MHz, SDK USB coexistence, PSRAM TLS and diagnostics disabled")


def full_payload(build, factory, application, table):
    """从同一板级构建收集完整镜像，校验地址和空文件系统尺寸。 / Collect one board build's complete images and check addresses/storage sizes."""
    expected = {"partition_table/partition-table.bin": 0x8000, "bootloader/bootloader.bin": 0,
                "ota_data_initial.bin": 0xf000, "pico_nano.bin": 0x20000}
    lines = (build / "flash_args").read_text(encoding="utf-8").splitlines()
    listed = {name: int(offset, 16) for offset, name in (line.split() for line in lines[1:] if line.strip())}
    if listed != expected:
        raise ValueError("Unexpected board flash addresses")
    payload = {"pico_nano.bin": application, "partition-table.bin": table,
               "bootloader.bin": (build / "bootloader/bootloader.bin").read_bytes(),
               "ota_data_initial.bin": (build / "ota_data_initial.bin").read_bytes(),
               "first-install/factory-data.bin": (factory / "factory-data.bin").read_bytes(),
               "first-install/factory-wallpaper.bin": (factory / "factory-wallpaper.bin").read_bytes(),
               "flash_args": (lines[0] + "\n0x0 bootloader.bin\n0x8000 partition-table.bin\n0xf000 ota_data_initial.bin\n0x20000 pico_nano.bin\n").encode()}
    if (not 0 < len(payload["bootloader.bin"]) <= 0x8000 or len(table) != 0xc00 or
            not 0 < len(application) <= 0x600000 or len(payload["ota_data_initial.bin"]) != 0x2000 or
            any(len(payload[name]) != 0x100000 for name in ("first-install/factory-data.bin", "first-install/factory-wallpaper.bin"))):
        raise ValueError("Invalid full image sizes; run factory-data before packaging")
    return payload


def validate_build_version(application, version, description):
    """拒绝将旧构建标成新版本，仍需发布流程确认源码出处。 / Reject stale builds labeled as a new version; release workflow still confirms source provenance."""
    binary_version = application[48:80].split(b"\0", 1)[0].decode("ascii")
    if description.get("target") != "esp32s3" or description.get("project_version") != version or binary_version != version:
        raise ValueError("Board build version/target mismatch; rebuild the committed board source before full packaging")


if __name__ == "__main__":
    main()
