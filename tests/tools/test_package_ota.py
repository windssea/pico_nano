"""以真实密码库检查签名覆盖及错误密钥。 / Test signature coverage and wrong keys with real cryptography."""
import pathlib
import struct
import sys
import unittest
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "tools"))
from package_ota import make_manifest, verify_manifest, public_bytes
try:
    from cryptography.exceptions import InvalidSignature
    from cryptography.hazmat.primitives.asymmetric import ec
except ImportError:
    ec = None


@unittest.skipIf(ec is None, "cryptography is available in the pinned ESP-IDF host-test image")
class OTAPackage(unittest.TestCase):
    def setUp(self):
        self.key = ec.generate_private_key(ec.SECP256R1())
        self.app = bytearray(1024)
        self.app[0], self.app[1], self.app[12] = 0xe9, 3, 9
        struct.pack_into("<I", self.app, 32, 0xabcd5432)
        self.app[48:53] = b"0.9.0"
        self.app[80:89] = b"pico_nano"
        self.boot = b"\xe9" + b"\0" * 99

    def test_real_signature_covers_every_payload_byte(self):
        data = make_manifest(bytes(self.app), self.boot, "0.9.0", "a" * 40, self.key)
        self.assertEqual(len(data), 576)
        self.assertEqual(len(public_bytes(self.key.public_key())), 65)
        verify_manifest(data, self.key.public_key())
        for i in range(8, 576):
            changed = bytearray(data)
            changed[i] ^= 1
            with self.assertRaises(InvalidSignature):
                verify_manifest(bytes(changed), self.key.public_key())
        with self.assertRaises(InvalidSignature):
            verify_manifest(data, ec.generate_private_key(ec.SECP256R1()).public_key())

    def test_wrong_image_identity_or_diagnostic_version_rejected(self):
        for version in ("0.9.0-diag", "0.9", "00.9.0", "1.2.3"):
            with self.assertRaises(ValueError):
                make_manifest(bytes(self.app), self.boot, version, "a" * 40, self.key)
        self.app[12] = 0
        with self.assertRaises(ValueError):
            make_manifest(bytes(self.app), self.boot, "0.9.0", "a" * 40, self.key)


if __name__ == "__main__":
    unittest.main()
