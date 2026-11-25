"""Direct NAND format/corruption tests using only the shipped 32 KiB prebuilt."""

import copy
import importlib.util
import json
import struct
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("nand_image", ROOT / "tools/nand_image.py")
tool = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(tool)


def application(size=1024):
    data = bytearray((i * 37 + (i >> 8)) & 255 for i in range(size))
    for i in range(8):
        struct.pack_into("<I", data, i * 4, 0xEA000000 | ((512 - i * 4 - 8) // 4))
    struct.pack_into("<I", data, 0x2C, tool.APP_ORIGIN)
    return bytes(data)


class NandImageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.profile = json.loads((ROOT / "prebuilt/r328/spinand.json").read_text())
        cls.boot0 = (ROOT / "prebuilt/r328/boot0-spinand.bin").read_bytes()

    def test_prebuilt_and_provenance(self):
        tool.check_boot0(self.boot0)
        self.assertEqual(tool.sha256(self.boot0), self.profile["boot0_sha256"])
        self.assertEqual(
            tool.sha256(self.boot0[0x38:0xB8]), self.profile["ddr"]["parameters_sha256"]
        )
        self.assertEqual(self.boot0[0x1F8:0x258], bytes(96))

    def test_configured_boot0_preserves_code_and_training(self):
        configured = tool.configure_boot0(self.boot0, self.profile, "PH0_PH1")
        tool.check_boot0(configured)
        allowed = (
            set(range(12, 16))
            | set(range(0x28, 0x2C))
            | set(range(0xBC, 0xCC))
            | set(range(0x1F8, 0x258))
        )
        self.assertTrue(
            all(a == b or i in allowed for i, (a, b) in enumerate(zip(self.boot0, configured)))
        )
        self.assertEqual(configured[0x38:0xB8], self.boot0[0x38:0xB8])
        self.assertEqual(tool.u32(configured, 0x28), 4)
        self.assertEqual(configured[0x1F8 + 28 : 0x1F8 + 36], bytes(self.profile["chip_id"]))
        self.assertEqual(tool.u32(configured, 0x1F8 + 8), 64)
        self.assertEqual(tool.u32(configured, 0x1F8 + 12), 1024)
        self.assertEqual(tool.u32(configured, 0x1F8 + 56), 8)
        self.assertEqual(tool.u32(configured, 0x1F8 + 60), 31)

    def test_both_reviewed_devices(self):
        for name, blocks, chip_id in (
            ("spinand.json", 1024, [0xC2, 0x12]),
            ("spinand-fs35.json", 4096, [0xCD, 0xEC, 0x11]),
        ):
            profile = json.loads((ROOT / "prebuilt/r328" / name).read_text())
            configured = tool.configure_boot0(self.boot0, profile, "PH0_PH1")
            self.assertEqual(tool.u32(configured, 0x1F8 + 12), blocks)
            self.assertEqual(configured[0x214 : 0x214 + len(chip_id)], bytes(chip_id))

    def test_uart_routes(self):
        for route, tx, rx in (
            ("PH0_PH1", [8, 0, 3, 1], [8, 1, 3, 1]),
            ("PB8_PB9", [2, 8, 5, 1], [2, 9, 5, 1]),
        ):
            configured = tool.configure_boot0(self.boot0, self.profile, route)
            self.assertEqual(configured[0xBC:0xC0], bytes(tx))
            self.assertEqual(configured[0xC4:0xC8], bytes(rx))
        with self.assertRaises(ValueError):
            tool.configure_boot0(self.boot0, self.profile, "PH0_PH2")

    def test_corrupt_prebuilt_rejected(self):
        for offset in (12, 0x38, 0x404, 0x6114, 0x63FC, 0x7FFF):
            bad = bytearray(self.boot0)
            bad[offset] ^= 1
            with self.assertRaises(ValueError):
                tool.configure_boot0(bad, self.profile, "PH0_PH1")
        with self.assertRaises(ValueError):
            tool.check_boot0(self.boot0[:-4])

    def test_geometry_change_rejected(self):
        for key, value in (
            ("page_size", 4096),
            ("boot0_data_per_page", 1024),
            ("package_first_block", 0),
            ("package_last_block", 4096),
            ("spi_operation_options", 0x100),
            ("boot0_blocks", [0]),
            ("chip_id", [0xEF, 0xAA, 0x21]),
        ):
            bad = copy.deepcopy(self.profile)
            bad[key] = value
            with self.assertRaisesRegex(ValueError, key):
                tool.configure_boot0(self.boot0, bad, "PH0_PH1")

    def test_package_is_single_direct_application(self):
        firmware = application(150001)
        package = tool.make_package(firmware)
        tool.check_stamped(package, 20)
        self.assertEqual(tool.u32(package, 32), 1)
        self.assertEqual(tool.u32(package, 36), len(package))
        self.assertEqual(package[64:128].split(b"\0", 1)[0], b"freertos")
        self.assertEqual(
            struct.unpack_from("<6I", package, 128), (512, len(firmware), 0, 3, tool.APP_ORIGIN, 0)
        )
        self.assertEqual(tool.u32(package, 60), 0x3B45494D)
        self.assertEqual(tool.u32(package, 428), 0x3B454949)
        self.assertEqual(len(package) % 4096, 0)
        self.assertEqual(package[512 : 512 + len(firmware)], firmware)
        self.assertEqual(package[512 + len(firmware) :], bytes(len(package) - 512 - len(firmware)))

    def test_bad_vectors_or_header_rejected(self):
        for offset, word in ((0, 0xEA7FFFFF), (0, 0xE1A00000), (0, 0xEA000000), (0x2C, 0x40800000)):
            firmware = bytearray(application())
            struct.pack_into("<I", firmware, offset, word)
            with self.assertRaises(ValueError):
                tool.make_package(firmware)
        for size in (0, 31, 512):
            with self.assertRaises(ValueError):
                tool.make_package(bytes(size))

    def test_physical_boot0_pages_and_all_package_copies(self):
        for size in (1024, 150001):
            output, package, boot0, report = tool.build_image(
                self.boot0, self.profile, application(size)
            )
            self.assertEqual(len(output), 32 * 128 * 1024)
            expected = bytearray(b"\xff" * len(output))
            for block in (0, 2, 4, 6):
                for i in range(16):
                    start = block * 0x20000 + i * 2048
                    expected[start : start + 2048] = boot0[i * 2048 : (i + 1) * 2048]
            span = (len(package) + 0x1FFFF) // 0x20000
            starts = list(range(8, 32 - span + 1, span))
            self.assertEqual([c["block"] for c in report["package_copies"]], starts)
            for block in starts:
                start = block * 0x20000
                expected[start : start + len(package)] = package
            # This checks every unused page, gap, padding and copy as well.
            self.assertEqual(output, expected)
            self.assertEqual(report["sha256"]["image"], tool.sha256(output))
            self.assertEqual(report["gic"], "SECURE / Group 0")

    def test_r328_rom_load_and_checksum(self):
        # R328 ROM 0x7194: page payload 2048, not the 1024 used by some SoCs.
        # 0x71c tries row 0; 0x57c then tries rows 128, 256, ... 896.
        # Keep this independent of profile fields and the packer's write loop.
        image, _, boot0, report = tool.build_image(self.boot0, self.profile, application())
        candidates = set(range(0, 1024, 128))
        for block in report["nand"]["boot0_blocks"]:
            first_row = block * 64
            self.assertIn(first_row, candidates)
            start = first_row * 2048
            header = image[start : start + 2048]
            self.assertEqual(header[4:12], b"eGON.BT0")
            length = struct.unpack_from("<I", header, 16)[0]
            self.assertEqual(length % 1024, 0)
            loaded = b"".join(
                image[row * 2048 : (row + 1) * 2048]
                for row in range(first_row, first_row + (length + 2047) // 2048)
            )[:length]
            self.assertEqual(loaded, boot0)
            tool.check_stamped(loaded, 12)

        # The former 1 KiB/page image passed programming/readback, but the ROM
        # includes FF gaps and misses half of boot0 when reading its 32 KiB.
        old = bytearray(b"\xff" * 65536)
        for index in range(32):
            old[index * 2048 : index * 2048 + 1024] = boot0[index * 1024 : (index + 1) * 1024]
        with self.assertRaisesRegex(ValueError, "checksum"):
            tool.check_stamped(old[:length], 12)

    def test_package_too_large_rejected(self):
        with self.assertRaisesRegex(ValueError, "boot area"):
            tool.build_image(self.boot0, self.profile, application(24 * 128 * 1024))

    def test_checksum_corruption(self):
        package = bytearray(tool.make_package(application()))
        package[700] ^= 1
        with self.assertRaisesRegex(ValueError, "checksum"):
            tool.check_stamped(package, 20)

    def test_elf_and_bin_must_agree(self):
        data = bytearray(84)
        data[:7] = b"\x7fELF\x01\x01\x01"
        struct.pack_into("<H", data, 18, 40)
        struct.pack_into("<II", data, 24, tool.APP_ORIGIN, 52)
        struct.pack_into("<HH", data, 42, 32, 1)
        struct.pack_into("<8I", data, 52, 1, 0, tool.APP_ORIGIN, tool.APP_ORIGIN, 32, 64, 5, 4)
        tool.validate_elf(data, tool.APP_ORIGIN, tool.APP_LENGTH, bytes(data[:32]))
        with self.assertRaisesRegex(ValueError, "BIN does not match"):
            tool.validate_elf(data, tool.APP_ORIGIN, tool.APP_LENGTH, bytes(32))
        for offset, value in ((24, 0x28000), (72, tool.APP_LENGTH + 1), (28, 4096)):
            bad = bytearray(data)
            struct.pack_into("<I", bad, offset, value)
            with self.assertRaises(ValueError):
                tool.validate_elf(bad, tool.APP_ORIGIN, tool.APP_LENGTH)


if __name__ == "__main__":
    unittest.main()
