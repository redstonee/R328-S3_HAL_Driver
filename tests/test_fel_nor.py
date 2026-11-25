"""Exercise stock-xfel orchestration without USB or Flash hardware."""

import contextlib
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
import fel_nor as tool
import nor_image
from test_nor_image import application


class FakeFEL:
    def __init__(self):
        self.memory = bytearray(0x40000)
        self.flash = bytearray(b"\x55" * tool.CAPACITY)
        self.operations = []
        self.calls = []
        self.wrong_chip = self.wrong_scratch = self.bad_upload = False
        self.bad_id = self.bad_echo = self.bad_completion = self.bad_verify = False
        self.status = 0
        self.fail_erase = False

    def invoke(self, xfel, *args):
        self.calls.append(args[0])
        if args[0] == "version":
            chip = "00185900" if self.wrong_chip else "00182100"
            scratch = "28000" if self.wrong_scratch else "27e00"
            return f"AWUSBFEX ID=0x{chip}(R328) scratchpad=0x{scratch}"
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
            assert address == tool.ENTRY
            request = list(tool.MESSAGE.unpack_from(self.memory, tool.MAILBOX))
            magic, sequence, operation, address, length, crc = request[:6]
            assert magic == tool.MAGIC and request[6] == 0xFFFFFFFF
            data = (
                bytes(self.memory[tool.BUFFER : tool.BUFFER + length])
                if operation == tool.PROGRAM
                else b""
            )
            assert zlib.crc32(data, zlib.crc32(struct.pack("<5I", *request[:5]))) == crc
            self.operations.append((operation, address, length))
            if self.fail_erase and operation == tool.ERASE:
                raise RuntimeError("USB exec failed")
            detail = 0xEF4018 if self.bad_id else 0xEF4017
            if self.status == 0:
                if operation == tool.ERASE:
                    self.flash[address : address + length] = b"\xff" * length
                elif operation == tool.PROGRAM:
                    self.flash[address : address + length] = data
                elif operation == tool.READ:
                    self.memory[tool.BUFFER : tool.BUFFER + length] = self.flash[
                        address : address + length
                    ]
                    if self.bad_verify:
                        self.memory[tool.BUFFER] ^= 1
            request[6:] = [self.status, detail, length + (1 if self.bad_completion else 0)]
            if self.bad_echo:
                request[1] = sequence - 1
            tool.MESSAGE.pack_into(self.memory, tool.MAILBOX, *request)
        else:
            raise AssertionError(args)
        return ""


class FELNorTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.helper_binary = self.root / "helper.bin"
        self.helper = bytes(64) + tool.FOOTER
        self.helper_binary.write_bytes(self.helper)
        self.helper_manifest = self.root / "helper.json"
        self.report = {
            "format": "r328-fel-nor-helper-v1",
            "binary": "helper.bin",
            "entry": tool.ENTRY,
            "mailbox": tool.MAILBOX,
            "buffer": tool.BUFFER,
            "buffer_size": tool.BUFFER_SIZE,
            "stack_bottom": tool.STACK_BOTTOM,
            "stack_top": tool.STACK_TOP,
            "chip_id": tool.CHIP_ID,
            "size": len(self.helper),
            "sha256": tool.digest(self.helper),
        }
        self.helper_manifest.write_text(json.dumps(self.report))
        profile = json.loads((ROOT / "prebuilt/r328/spinor.json").read_text())
        image, _, _, report = nor_image.build_image(
            (ROOT / "prebuilt/r328/boot0-spinand.bin").read_bytes(),
            profile,
            bytes.fromhex("00207047"),
            application(),
            "PH0_PH1",
        )
        self.image_data = image
        self.image, self.manifest = self.root / "nor.bin", self.root / "nor.json"
        self.image.write_bytes(image)
        self.manifest.write_text(json.dumps(report))
        self.fake = FakeFEL()

    def run_tool(self, command="flash", dry_run=False):
        with (
            patch.object(tool, "invoke", side_effect=self.fake.invoke),
            contextlib.redirect_stdout(io.StringIO()),
            contextlib.redirect_stderr(io.StringIO()),
        ):
            tool.run(command, self.helper_manifest, "test-xfel", self.image, self.manifest, dry_run)

    def test_complete_flash_then_full_verify(self):
        self.run_tool()
        self.assertEqual(self.fake.flash, self.image_data)
        operations = self.fake.operations
        self.assertEqual(operations[0], (tool.PROBE, 0, 0))
        erases = [op for op in operations if op[0] == tool.ERASE]
        reads = [op for op in operations if op[0] == tool.READ]
        self.assertEqual(
            erases,
            [(tool.ERASE, a, tool.ERASE_SIZE) for a in range(0, tool.CAPACITY, tool.ERASE_SIZE)],
        )
        self.assertEqual(
            reads,
            [(tool.READ, a, tool.BUFFER_SIZE) for a in range(0, tool.CAPACITY, tool.BUFFER_SIZE)],
        )
        first_program = next(i for i, op in enumerate(operations) if op[0] == tool.PROGRAM)
        first_read = next(i for i, op in enumerate(operations) if op[0] == tool.READ)
        self.assertLess(128, first_program)
        self.assertTrue(all(op[0] == tool.PROGRAM for op in operations[first_program:first_read]))
        self.assertEqual(self.fake.memory[tool.ENTRY : tool.ENTRY + len(self.helper)], self.helper)

    def test_probe_and_verify_do_not_erase(self):
        self.run_tool("probe")
        self.assertEqual(self.fake.operations, [(tool.PROBE, 0, 0)])
        self.fake.flash[:] = self.image_data
        self.run_tool("verify")
        self.assertFalse(any(op[0] in (tool.ERASE, tool.PROGRAM) for op in self.fake.operations))

    def test_dry_run_and_changed_files_never_access_usb(self):
        for command in ("probe", "flash", "verify"):
            self.run_tool(command, dry_run=True)
        self.assertEqual(self.fake.calls, [])
        self.helper_binary.write_bytes(self.helper + b"changed")
        with self.assertRaisesRegex(ValueError, "Helper differs"):
            self.run_tool()
        self.helper_binary.write_bytes(self.helper)
        self.image.write_bytes(bytes(tool.CAPACITY))
        with self.assertRaisesRegex(ValueError, "image differs"):
            self.run_tool()
        self.assertEqual(self.fake.calls, [])

    def test_bad_layout_and_footer_rejected(self):
        self.report["buffer"] = 0x3C000
        self.helper_manifest.write_text(json.dumps(self.report))
        with self.assertRaisesRegex(ValueError, "layout"):
            tool.load_helper(self.helper_manifest)
        with self.assertRaisesRegex(ValueError, "footer"):
            tool.check_helper(bytes(128))
        with self.assertRaisesRegex(ValueError, "window"):
            tool.check_helper(bytes(0x1001))

    def test_wrong_chip_and_scratchpad_never_download(self):
        for field in ("wrong_chip", "wrong_scratch"):
            self.fake = FakeFEL()
            setattr(self.fake, field, True)
            with self.assertRaisesRegex(ValueError, "nothing downloaded"):
                self.run_tool()
            self.assertEqual(self.fake.calls, ["version"])

    def test_bad_helper_readback_never_executes(self):
        self.fake.bad_upload = True
        with self.assertRaisesRegex(ValueError, "readback mismatch"):
            self.run_tool()
        self.assertNotIn("exec", self.fake.calls)

    def test_bad_probe_never_erases(self):
        for field, pattern in (
            ("bad_id", "JEDEC"),
            ("bad_echo", "echo"),
            ("bad_completion", "Incomplete"),
        ):
            self.fake = FakeFEL()
            setattr(self.fake, field, True)
            with self.assertRaisesRegex(ValueError, pattern):
                self.run_tool()
            self.assertEqual(len(self.fake.operations), 1)
        for status in tool.ERRORS:
            self.fake = FakeFEL()
            self.fake.status = status
            with self.assertRaisesRegex(ValueError, "NOR operation"):
                self.run_tool()
            self.assertEqual(len(self.fake.operations), 1)

    def test_verify_reports_exact_failure_address(self):
        self.fake.flash[:] = self.image_data
        self.fake.bad_verify = True
        with self.assertRaisesRegex(ValueError, "mismatch at 0x000000"):
            self.run_tool("verify")

    def test_failed_erase_is_not_retried(self):
        self.fake.fail_erase = True
        with self.assertRaisesRegex(RuntimeError, "USB exec failed"):
            self.run_tool()
        self.assertEqual(
            self.fake.operations, [(tool.PROBE, 0, 0), (tool.ERASE, 0, tool.ERASE_SIZE)]
        )


if __name__ == "__main__":
    unittest.main()
