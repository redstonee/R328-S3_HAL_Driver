"""W25Q64 linear layout and hash-locked boot0 patch boundaries."""

import copy
import json
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import nor_image as tool
from nand_image import check_stamped, u32


def application(size=1024):
    data = bytearray((bytes(range(256)) * ((size + 255) // 256))[:size])
    for i in range(8):
        struct.pack_into("<I", data, i * 4, 0xEA000000 | ((512 - i * 4 - 8) // 4))
    struct.pack_into("<I", data, 0x2C, tool.APP_ORIGIN)
    return bytes(data)


class NorImageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.profile = json.loads((ROOT / "prebuilt/r328/spinor.json").read_text())
        cls.boot0 = (ROOT / "prebuilt/r328/boot0-spinand.bin").read_bytes()
        # Metadata/patch fixture, not the production PIO implementation.
        cls.reader = bytes.fromhex("00207047")

    def test_patch_preserves_ddr_and_handoff(self):
        result = tool.configure_boot0(self.boot0, self.profile, self.reader, "PH0_PH1")
        tool.check_boot0(result)
        allowed = (
            set(range(12, 16))
            | set(range(0x28, 0x2C))
            | set(range(0xBC, 0xCC))
            | set(range(tool.PATCH_OFFSET, tool.DISPATCH_OFFSET + 8))
        )
        for offset, (before, after) in enumerate(zip(self.boot0, result)):
            self.assertTrue(before == after or offset in allowed, f"Unexpected patch {offset:#x}")
        self.assertEqual(result[0x38:0xB8], self.boot0[0x38:0xB8])
        self.assertEqual(result[0x60DC:], self.boot0[0x60DC:])
        self.assertEqual(u32(result, 0x28), 3)
        self.assertEqual(result[0x5564:0x5568], self.reader)
        self.assertEqual(struct.unpack_from("<HHI", result, 0x60C0), (0x4B00, 0x4718, 0x25565))

    def test_uart_routes(self):
        for route, tx, rx in (
            ("PH0_PH1", b"\x08\x00\x03\x01", b"\x08\x01\x03\x01"),
            ("PB8_PB9", b"\x02\x08\x05\x01", b"\x02\x09\x05\x01"),
        ):
            result = tool.configure_boot0(self.boot0, self.profile, self.reader, route)
            self.assertEqual(result[0xBC:0xC0], tx)
            self.assertEqual(result[0xC4:0xC8], rx)
            tool.check_boot0(result)

    def test_geometry_and_prebuilt_guards(self):
        for field, value in (
            ("capacity", 0x1000000),
            ("package_offset", 0x8000),
            ("spi_clock_hz", 24000000),
            ("address_bytes", 4),
            ("read_opcode", 0x0B),
        ):
            bad = copy.deepcopy(self.profile)
            bad[field] = value
            with self.assertRaisesRegex(ValueError, field):
                tool.configure_boot0(self.boot0, bad, self.reader, "PH0_PH1")
        for offset in (0x38, 0x5564, 0x60C0, 0x6114, 0x7FFF):
            bad = bytearray(self.boot0)
            bad[offset] ^= 1
            with self.assertRaisesRegex(ValueError, "SHA256"):
                tool.configure_boot0(bad, self.profile, self.reader, "PH0_PH1")

    def test_reader_region_guards(self):
        for reader in (b"", bytes(0xB5D)):
            with self.assertRaisesRegex(ValueError, "patch region"):
                tool.configure_boot0(self.boot0, self.profile, reader, "PH0_PH1")

    def test_whole_linear_image(self):
        for size in (1024, 150001):
            firmware = application(size)
            image, package, boot0, report = tool.build_image(
                self.boot0, self.profile, self.reader, firmware, "PH0_PH1"
            )
            expected = bytearray(b"\xff" * 0x800000)
            expected[:32768] = boot0
            expected[0x10000 : 0x10000 + len(package)] = package
            self.assertEqual(image, expected)
            self.assertEqual(package[512 : 512 + size], firmware)
            check_stamped(package, 20)
            self.assertEqual(report["sha256"]["image"], tool.sha256(image))
            self.assertEqual(report["package_offset"], 0x10000)
            self.assertFalse(report["hardware_tested"])

    def test_flash_capacity_boundary(self):
        maximum = 0x800000 - 0x10000 - 512
        image, package, _, _ = tool.build_image(
            self.boot0, self.profile, self.reader, application(maximum), "PH0_PH1"
        )
        self.assertEqual(len(image), 0x800000)
        self.assertEqual(len(package), 0x7F0000)
        with self.assertRaisesRegex(ValueError, "exceeds W25Q64"):
            tool.build_image(
                self.boot0, self.profile, self.reader, application(maximum + 1), "PH0_PH1"
            )

    def test_wrong_application_header_rejected(self):
        bad = bytearray(application())
        struct.pack_into("<I", bad, 0x2C, 0x40800000)
        with self.assertRaisesRegex(ValueError, "header"):
            tool.build_image(self.boot0, self.profile, self.reader, bad, "PH0_PH1")


if __name__ == "__main__":
    unittest.main()
