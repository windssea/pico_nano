"""已装 PicoNano 的设备保留布局升级。 / Preserve layout when upgrading an existing PicoNano device."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import sys


def verify_package(root):
    """写入前检查固定文件与摘要。 / Check fixed files and digests before writing."""
    manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
    for name in ("pico_nano.bin", "expected-partition-table.bin"):
        data = (root / name).read_bytes()
        entry = manifest["files"][name]
        if len(data) != entry["bytes"] or hashlib.sha256(data).hexdigest() != entry["sha256"]:
            raise ValueError(f"Package checksum mismatch: {name}")
    if not 0 < (root / "pico_nano.bin").stat().st_size <= 0x600000:
        raise ValueError("Application exceeds the existing OTA slot")
    expected = (root / "expected-partition-table.bin").read_bytes()
    if len(expected) != 0xC00:
        raise ValueError("Expected partition table must be 3072 bytes")
    return manifest, expected


def run(command):
    """保留命令失败，不继续写入。 / Stop on command failure instead of continuing writes."""
    print(subprocess.list2cmdline(command), flush=True)
    subprocess.run(command, check=True)


def flash(root, port, baud, before):
    """只读核对分区后，仅写应用。 / Read-check the partition table, then write only the application."""
    manifest, expected = verify_package(root)
    print(f"PicoNano source: {manifest['source_commit']}", flush=True)
    base = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", port,
            "--baud", str(baud), "--connect-attempts", "3"]
    # 读回保留在包内便于排障，不改变设备分区。 / Keep readback for diagnosis without changing device partitions.
    readback = root / "device-partition-readback.bin"
    if readback.exists():
        raise ValueError("Readback already exists; rename it before retrying to retain the previous evidence")
    run(base + ["--before", before, "--after", "no-reset", "read-flash",
                "0x8000", "0x1000", str(readback)])
    actual = readback.read_bytes()
    if len(actual) != 0x1000 or actual[:0xC00] != expected:
        raise ValueError("Device partition table differs; no firmware was written")
    # esptool自动校验写入数据；原生USB用watchdog复位退出下载模式。 / esptool verifies writes; watchdog reset exits native USB download mode.
    run(base + ["--before", "no-reset", "--after", "watchdog-reset", "write-flash",
                "0x20000", str(root / "pico_nano.bin")])
    print("Write and device hash verification completed. Check device startup separately.", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parent)
    parser.add_argument("--port", help="Device port on this computer, e.g. COM5")
    parser.add_argument("--baud", type=int, choices=(115200, 460800), default=115200)
    parser.add_argument("--before", choices=("usb-reset", "default-reset", "no-reset"), default="usb-reset")
    parser.add_argument("--flash", action="store_true", help="Read-check device partitions and write application")
    args = parser.parse_args()
    try:
        verify_package(args.package)
        if args.flash:
            if not args.port:
                parser.error("--flash requires --port")
            flash(args.package, args.port, args.baud, args.before)
        else:
            print("Package checks passed; no device was accessed.")
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as exc:
        print(f"STOP: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
