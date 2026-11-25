"""NAND FEL orchestration against a physical-block/marker model; no USB access."""

import contextlib
import copy
import io
import json
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fel_nand as tool
import fel_nor as transport
import nand_image
from test_nand_image import application


class FakeFEL:
    def __init__(self):
        self.memory = bytearray(0x40000)
        self.flash = bytearray(b"\x55" * (tool.IMAGE_SIZE + tool.BLOCK_SIZE))
        self.bad = {0, 9, 12}
        self.operations = []
        self.calls = []
        self.id = 0xC212
        self.wrong_chip = self.bad_upload = self.bad_echo = self.bad_completion = False
        self.fail_erase = self.bad_verify = self.corrupt_markers = False
        self.status = 0

    def invoke(self, xfel, *args):
        self.calls.append(args[0])
        if args[0] == "version":
            chip = "00185900" if self.wrong_chip else "00182100"
            return f"AWUSBFEX ID=0x{chip}(R328) scratchpad=0x27e00"
        address = int(args[1], 0)
        if args[0] == "write":
            data = Path(args[2]).read_bytes()
            self.memory[address : address + len(data)] = data
        elif args[0] == "read":
            length = int(args[2])
            data = self.memory[address : address + length]
            if self.bad_upload and address == tool.ENTRY:
                data[0] ^= 1
            Path(args[3]).write_bytes(data)
        elif args[0] == "exec":
            request = list(transport.MESSAGE.unpack_from(self.memory, tool.MAILBOX))
            magic, _, operation, address, length, crc = request[:6]
            assert magic == tool.MAGIC and request[6] == 0xFFFFFFFF
            payload = (
                bytes(self.memory[tool.BUFFER : tool.BUFFER + length])
                if operation == tool.PROGRAM
                else b""
            )
            assert zlib.crc32(payload, zlib.crc32(struct.pack("<5I", *request[:5]))) == crc
            self.operations.append((operation, address, length))
            if self.fail_erase and operation == tool.ERASE:
                raise RuntimeError("USB exec response lost")
            detail = self.id
            if operation == tool.BAD_BLOCK:
                damaged = (
                    self.corrupt_markers
                    and address == tool.BLOCK_SIZE
                    and any(op == tool.PROGRAM for op, _, _ in self.operations)
                )
                detail = 0xFF00 if address // tool.BLOCK_SIZE in self.bad or damaged else 0xFFFF
            elif operation == tool.ERASE:
                assert address % tool.BLOCK_SIZE == 0 and length == tool.BLOCK_SIZE
                assert address // tool.BLOCK_SIZE not in self.bad and address < tool.IMAGE_SIZE
                self.flash[address : address + length] = b"\xff" * length
            elif operation == tool.PROGRAM:
                assert address % tool.PAGE_SIZE == 0 and length % tool.PAGE_SIZE == 0
                assert (
                    address // tool.BLOCK_SIZE not in self.bad
                    and address + length <= tool.IMAGE_SIZE
                )
                self.flash[address : address + length] = payload
            elif operation == tool.READ:
                assert address // tool.BLOCK_SIZE not in self.bad
                data = self.flash[address : address + length]
                if self.bad_verify:
                    data[17] ^= 1
                self.memory[tool.BUFFER : tool.BUFFER + length] = data
            request[6:] = [self.status, detail, length - 1 if self.bad_completion else length]
            if self.bad_echo:
                request[1] += 1
            transport.MESSAGE.pack_into(self.memory, tool.MAILBOX, *request)
        else:
            raise AssertionError(args)
        return ""


class NandFelTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="r328-test-fel-nand-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.helper = self.root / "fel-nand-helper.bin"
        self.helper.write_bytes(bytes(64) + tool.FOOTER)
        self.helper_manifest = self.root / "helper.json"
        self.helper_manifest.write_text(
            json.dumps(
                {
                    "format": "r328-fel-nand-helper-v1",
                    "binary": self.helper.name,
                    "entry": tool.ENTRY,
                    "mailbox": tool.MAILBOX,
                    "buffer": tool.BUFFER,
                    "buffer_size": tool.BUFFER_SIZE,
                    "stack_bottom": transport.STACK_BOTTOM,
                    "stack_top": transport.STACK_TOP,
                    "chip_id": transport.CHIP_ID,
                    "size": self.helper.stat().st_size,
                    "sha256": transport.digest(self.helper.read_bytes()),
                }
            )
        )
        profile = json.loads((ROOT / "prebuilt/r328/spinand.json").read_text())
        self.data, _, _, self.report = nand_image.build_image(
            (ROOT / "prebuilt/r328/boot0-spinand.bin").read_bytes(),
            profile,
            application(150001),
        )
        self.image, self.manifest = self.root / "image.bin", self.root / "image.json"
        self.image.write_bytes(self.data)
        self.manifest.write_text(json.dumps(self.report))
        self.fake = FakeFEL()
        self.output = io.StringIO()

    def run_tool(self, command="flash", dry_run=False):
        with (
            patch.object(transport, "invoke", self.fake.invoke),
            contextlib.redirect_stdout(self.output),
            contextlib.redirect_stderr(self.output),
        ):
            tool.run(command, self.helper_manifest, "fake-xfel", self.image, self.manifest, dry_run)

    def test_flash_preserves_bad_blocks_physical_offsets_and_tail(self):
        self.run_tool()
        self.assertIn("NAND readback PASS", self.output.getvalue())
        for block in range(33):
            base = block * tool.BLOCK_SIZE
            expected = (
                b"\x55" * tool.BLOCK_SIZE
                if block in self.fake.bad or block == 32
                else self.data[base : base + tool.BLOCK_SIZE]
            )
            self.assertEqual(self.fake.flash[base : base + tool.BLOCK_SIZE], expected)
        erases = [
            address // tool.BLOCK_SIZE
            for op, address, _ in self.fake.operations
            if op == tool.ERASE
        ]
        self.assertEqual(erases, sorted(set(range(32)) - self.fake.bad))

    def test_verify_has_no_mutations(self):
        self.fake.flash[: tool.IMAGE_SIZE] = self.data
        self.run_tool("verify")
        self.assertTrue(
            all(op not in (tool.ERASE, tool.PROGRAM) for op, _, _ in self.fake.operations)
        )

    def test_no_surviving_boot0_or_multiblock_package_cancels_erase(self):
        for bad in (set(range(8)), set(range(8, 32, 2))):
            self.fake = FakeFEL()
            self.fake.bad = bad
            with self.assertRaisesRegex(ValueError, "No complete"):
                self.run_tool()
            self.assertTrue(
                all(op not in (tool.ERASE, tool.PROGRAM) for op, _, _ in self.fake.operations)
            )

    def test_probe_only_id_and_markers(self):
        self.run_tool("probe")
        self.assertEqual(
            [op for op, _, _ in self.fake.operations], [transport.PROBE] + [tool.BAD_BLOCK] * 32
        )

    def test_bad_identity_upload_and_echo_prevent_erasing(self):
        for field, value in (
            ("id", 0xC222),
            ("wrong_chip", True),
            ("bad_upload", True),
            ("bad_echo", True),
            ("bad_completion", True),
            ("status", 10),
        ):
            self.fake = FakeFEL()
            setattr(self.fake, field, value)
            with self.assertRaises((ValueError, struct.error)):
                self.run_tool()
            self.assertTrue(all(op != tool.ERASE for op, _, _ in self.fake.operations))

    def test_exec_failure_stops_without_retry(self):
        self.fake.fail_erase = True
        with self.assertRaisesRegex(RuntimeError, "USB exec"):
            self.run_tool()
        self.assertEqual(sum(op == tool.ERASE for op, _, _ in self.fake.operations), 1)
        self.assertIn("partially programmed", self.output.getvalue())

    def test_verify_error_reports_physical_address(self):
        self.fake.flash[: tool.IMAGE_SIZE] = self.data
        self.fake.bad_verify = True
        with self.assertRaisesRegex(ValueError, "physical 0x020011"):
            self.run_tool("verify")

    def test_marker_damage_is_not_success(self):
        self.fake.corrupt_markers = True
        with self.assertRaisesRegex(ValueError, "markers changed"):
            self.run_tool()

    def test_invalid_inputs_and_dry_run_never_touch_usb(self):
        self.run_tool(dry_run=True)
        self.assertFalse(self.fake.calls)
        for field, value in (("model", "FS35ND04G-S2Y2"), ("blocks_per_die", 4096)):
            report = copy.deepcopy(self.report)
            report["nand"][field] = value
            self.manifest.write_text(json.dumps(report))
            with self.assertRaises(ValueError):
                self.run_tool()
            self.assertFalse(self.fake.calls)
        self.manifest.write_text(json.dumps(self.report))
        self.image.write_bytes(self.data[:-1] + b"\x00")
        with self.assertRaisesRegex(ValueError, "differs"):
            self.run_tool()
        self.assertFalse(self.fake.calls)

    def test_old_1k_boot0_layout_rejected_before_usb(self):
        # A self-consistent old image/hash must still be refused, because the
        # R328 ROM reads full 2 KiB pages and cannot boot this representation.
        data = bytearray(self.data)
        boot0 = bytes(data[:32768])
        data[: 8 * tool.BLOCK_SIZE] = b"\xff" * (8 * tool.BLOCK_SIZE)
        for block in range(8):
            for index in range(32):
                start = block * tool.BLOCK_SIZE + index * 2048
                data[start : start + 1024] = boot0[index * 1024 : (index + 1) * 1024]
        report = copy.deepcopy(self.report)
        report["nand"]["boot0_data_per_page"] = 1024
        report["nand"]["boot0_blocks"] = list(range(8))
        report["sha256"]["image"] = transport.digest(data)
        self.image.write_bytes(data)
        self.manifest.write_text(json.dumps(report))
        with self.assertRaisesRegex(ValueError, "layout or manifest"):
            self.run_tool()
        self.assertFalse(self.fake.calls)


if __name__ == "__main__":
    unittest.main()
