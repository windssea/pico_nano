"""已装 PicoNano 的设备保留布局升级。 / Preserve layout when upgrading an existing PicoNano device."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import sys
import struct


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
    """只读核对分区后写应用；用过OTA时另清空otadata。 / Read-check the partition table, write the application, and blank otadata only after OTA use."""
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
    # ROM串口无法证明实际运行槽；用过OTA时写完ota_0再清空otadata，让启动确定回到ota_0。
    # ROM serial cannot prove the running slot; after OTA use, blank otadata once ota_0 is written so boot deterministically returns to ota_0.
    ranges = []
    for offset in range(0, len(expected), 32):
        if expected[offset:offset+2] != b"\xaa\x50":
            break
        if expected[offset+2:offset+4] == b"\x01\x00":
            ranges.append(struct.unpack_from("<II", expected, offset+4))
    if len(ranges) != 1 or ranges[0][1] != 0x2000 or ranges[0][0] % 0x1000 or not 0x9000 <= ranges[0][0] <= 0xffe000:
        raise ValueError("Known layout has no valid OTA selection range; no firmware was written")
    ota_readback = root / "device-otadata-readback.bin"
    if ota_readback.exists():
        raise ValueError("OTA readback already exists; retain it separately before retrying")
    run(base + ["--before", "no-reset", "--after", "no-reset", "read-flash", hex(ranges[0][0]), "0x2000", str(ota_readback)])
    ota = ota_readback.read_bytes()
    if len(ota) != 0x2000:
        raise ValueError("OTA selection read is incomplete; no firmware was written")
    reset_ota = ota != b"\xff" * 0x2000
    # esptool自动校验写入数据；原生USB用watchdog复位退出下载模式。先写应用后清选择，中途断电仍启动原有效槽。
    # esptool verifies writes; watchdog reset exits native USB download mode. App first, selection last: a power loss in between still boots the previous valid slot.
    run(base + ["--before", "no-reset", "--after", "no-reset" if reset_ota else "watchdog-reset", "write-flash",
                "0x20000", str(root / "pico_nano.bin")])
    if reset_ota:
        print("OTA selection was in use; blanking it so the device boots ota_0.", flush=True)
        run(base + ["--before", "no-reset", "--after", "no-reset", "erase-region", hex(ranges[0][0]), "0x2000"])
        blanked = root / "device-otadata-after.bin"
        blanked.unlink(missing_ok=True)
        run(base + ["--before", "no-reset", "--after", "watchdog-reset", "read-flash", hex(ranges[0][0]), "0x2000", str(blanked)])
        if blanked.read_bytes() != b"\xff" * 0x2000:
            raise ValueError("OTA selection did not read back blank; rerun the upgrade before using the device")
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
