"""为已构建应用生成Gitee OTA附件；签名私钥不进入仓库。 / Create Gitee OTA attachments for a built application; never place signing secrets in the repository."""
import argparse
import hashlib
import json
import pathlib
import re
import struct

ROOT = pathlib.Path(__file__).resolve().parents[1]
GITEE_ROOT = "https://gitee.com/windssea/pico_nano/releases/download/"
PAYLOAD_BYTES, MANIFEST_BYTES = 512, 576
MAX_IMAGE = 0x600000 * 9 // 10


def public_bytes(key):
    """固定未压缩P-256公钥编码。 / Fixed uncompressed P-256 public key encoding."""
    from cryptography.hazmat.primitives import serialization
    return key.public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)


def verify_manifest(data, public_key):
    """独立验证设备所用512字节载荷及64字节签名。 / Independently verify the device's 512-byte payload and 64-byte signature."""
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import ec, utils
    if len(data) != MANIFEST_BYTES or data[:8] != b"PNOTA1\r\n":
        raise ValueError("Invalid OTA manifest envelope")
    if not isinstance(public_key, ec.EllipticCurvePublicKey) or not isinstance(public_key.curve, ec.SECP256R1):
        raise ValueError("OTA requires an ECDSA P-256 public key")
    signature = utils.encode_dss_signature(int.from_bytes(data[512:544], "big"), int.from_bytes(data[544:], "big"))
    public_key.verify(signature, data[:512], ec.ECDSA(hashes.SHA256()))
    return data[:512]


def make_manifest(application, bootloader, version, commit, private_key, notes=""):
    """签名绑定应用、资格引导、硬件/布局及不可变下载地址。 / Bind the signed image, qualified bootloader, hardware/layout and immutable download URL."""
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import ec, utils
    if not isinstance(private_key, ec.EllipticCurvePrivateKey) or not isinstance(private_key.curve, ec.SECP256R1):
        raise ValueError("OTA requires an ECDSA P-256 private key")
    if not re.fullmatch(r"(?:0|[1-9][0-9]{0,2})\.(?:0|[1-9][0-9]{0,2})\.(?:0|[1-9][0-9]{0,2})", version) or not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Invalid product version or exact source commit")
    if not 288 <= len(application) <= MAX_IMAGE or not 24 <= len(bootloader) <= 0x8000:
        raise ValueError("Application headroom or bootloader size is invalid")
    if (application[0] != 0xe9 or application[12:14] != b"\x09\x00" or
            struct.unpack_from("<I", application, 32)[0] != 0xabcd5432 or
            application[48:80].split(b"\0", 1)[0] != version.encode("ascii") or
            application[80:112].split(b"\0", 1)[0] != b"pico_nano" or bootloader[0] != 0xe9):
        raise ValueError("Product app header/version/project or bootloader magic is invalid")
    payload = bytearray(PAYLOAD_BYTES)
    payload[:8] = b"PNOTA1\r\n"
    struct.pack_into("<8I", payload, 8, 1, len(application), 1, 9, 1, 1, 1, len(bootloader))
    url = f"{GITEE_ROOT}v{version}/PicoNano-{commit[:7]}-ota.bin"
    for offset, width, text in ((40, 32, version), (176, 256, url), (432, 64, notes)):
        encoded = text.encode("utf-8")
        if len(encoded) >= width or b"\0" in encoded:
            raise ValueError("OTA text field is too long or contains NUL")
        payload[offset:offset + len(encoded)] = encoded
    payload[72:112] = commit.encode("ascii")
    payload[112:144] = hashlib.sha256(application).digest()
    payload[144:176] = hashlib.sha256(bootloader).digest()
    signature = private_key.sign(bytes(payload), ec.ECDSA(hashes.SHA256()))
    r, s = utils.decode_dss_signature(signature)
    manifest = bytes(payload) + r.to_bytes(32, "big") + s.to_bytes(32, "big")
    verify_manifest(manifest, private_key.public_key())
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--release", required=True, type=pathlib.Path, help="New product upgrade-package directory")
    parser.add_argument("--private-key", required=True, type=pathlib.Path, help="Private P-256 PEM kept outside tracked source")
    parser.add_argument("--migration-bootloader", required=True, type=pathlib.Path, help="Qualified rollback bootloader from the first USB migration; retain for compatible future releases")
    parser.add_argument("--board-config", type=pathlib.Path, default=ROOT / "build-board/sdkconfig")
    parser.add_argument("--notes", default="")
    args = parser.parse_args()
    from cryptography.hazmat.primitives import serialization
    private = serialization.load_pem_private_key(args.private_key.read_bytes(), password=None)
    info = json.loads((args.release / "manifest.json").read_text(encoding="utf-8"))
    config = args.board_config.read_text(encoding="utf-8")
    expected_key = re.search(r'^CONFIG_PN_OTA_PUBLIC_KEY_HEX="([0-9a-fA-F]+)"$', config, re.MULTILINE)
    if ("CONFIG_PN_DEVICE_OTA=y" not in config or "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y" not in config or
            not expected_key or expected_key[1].lower() != public_bytes(private.public_key()).hex() or
            "CONFIG_PN_TEST_CONSOLE=y" in config):
        raise SystemExit("Build must enable rollback/OTA, disable diagnostics, and trust this signing public key")
    app = (args.release / "pico_nano.bin").read_bytes()
    entry = info["files"]["pico_nano.bin"]
    if (info.get("brand") != "PicoNano" or info.get("target") != "esp32s3" or info.get("board") != "RDP-G01-W" or
            hashlib.sha256(app).hexdigest() != entry["sha256"] or len(app) != entry["bytes"]):
        raise SystemExit("Existing product package identity/digest mismatch")
    manifest = make_manifest(app, args.migration_bootloader.read_bytes(), info["version"], info["source_commit"], private, args.notes)
    out = args.release / "ota"
    # 不覆盖已有签名产物；私钥和本地资格引导文件从不复制到发布目录。 / Never overwrite signed artifacts or copy private keys/local bootloader inputs into the release.
    out.mkdir(exist_ok=False)
    assets = {f"PicoNano-{info['source_commit'][:7]}-ota.bin": app, "PicoNano-ota-manifest.bin": manifest}
    for name, data in assets.items():
        (out / name).write_bytes(data)
    # 公钥仅供发布前独立验签；设备信任锚来自固件配置，不能由下载的公钥替换。
    # This public key supports release verification; downloaded keys never replace the firmware trust anchor.
    (out / "release-public.pem").write_bytes(private.public_key().public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
    (out / "SHA256SUMS").write_text("".join(f"{hashlib.sha256(data).hexdigest()}  {name}\n" for name, data in assets.items()), encoding="ascii")
    print(f"Signed OTA attachments: {out}; publish the immutable image before its manifest")


if __name__ == "__main__":
    main()
