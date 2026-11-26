"""Test FEL DDR packaging, large transfers and failure handling without USB."""

import contextlib
import io
import json
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fel_dram as tool


def elf_for(data, address, memory_size=None):
    header = bytearray(256)
    header[:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into("<H", header, 18, 40)
    struct.pack_into("<II", header, 24, address, 52)
    struct.pack_into("<HH", header, 42, 32, 1)
    struct.pack_into(
        "<8I", header, 52, 1, 256, address, address, len(data), memory_size or len(data), 7, 4
    )
    return bytes(header) + data


class FELDRAMTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix="r328-fel-ddr-test-")
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.binary, self.elf, self.helper, self.helper_elf, self.vendor, self.manifest = [
            self.root / name
            for name in (
                "firmware.bin",
                "firmware.elf",
                "helper.bin",
                "helper.elf",
                "vendor.bin",
                "fel.json",
            )
        ]
        self.app = struct.pack("<8I", *([0xEA000006] * 8)) + bytes(tool.CHUNK_SIZE * 2 + 9)
        self.code = bytes(64) + tool.FOOTER
        self.binary.write_bytes(self.app)
        self.elf.write_bytes(elf_for(self.app, tool.DDR_BASE, tool.DDR_SIZE))
        self.helper.write_bytes(self.code)
        self.helper_elf.write_bytes(elf_for(self.code, tool.ENTRY))
        self.pack()
        self.calls = []
        self.memory = {}
        self.status, self.size_mib = 2, 128
        self.corrupt_address = None
        self.fail_exec = None
        self.version = "AWUSBFEX ID=0x00182100(R328) scratchpad=0x00027e00"

    def pack(self):
        tool.package(
            self.binary,
            self.elf,
            self.helper,
            self.helper_elf,
            ROOT / "prebuilt/r328/boot0-spinand.bin",
            self.vendor,
            self.manifest,
            tool.DDR_BASE,
            tool.DDR_SIZE,
        )

    def invoke(self, xfel, *args):
        self.assertEqual(xfel, "test-xfel")
        self.calls.append(args)
        operation = args[0]
        self.assertIn(operation, ("version", "write", "read", "exec"))
        if operation == "version":
            return self.version
        address = int(args[1], 0)
        if operation == "write":
            self.memory[address] = Path(args[2]).read_bytes()
            self.assertLessEqual(len(self.memory[address]), tool.CHUNK_SIZE)
        elif operation == "read":
            data = self.memory[address]
            if address == self.corrupt_address:
                data = bytes([data[0] ^ 0xFF]) + data[1:]
            self.assertEqual(len(data), int(args[2]))
            Path(args[3]).write_bytes(data)
        elif operation == "exec":
            if address == self.fail_exec:
                raise subprocess.TimeoutExpired([xfel, *args], 20)
            if address == tool.ENTRY:
                self.memory[tool.MAILBOX] = tool.MESSAGE.pack(
                    tool.MAGIC, tool.ABI, self.status, self.size_mib
                )
        return ""

    def run_tool(self, **kwargs):
        with patch.object(tool, "invoke", side_effect=self.invoke):
            tool.run(self.manifest, "test-xfel", **kwargs)

    def test_large_application_sequence(self):
        self.run_tool()
        execs = [call for call in self.calls if call[0] == "exec"]
        self.assertEqual(execs, [("exec", hex(tool.ENTRY)), ("exec", hex(tool.DDR_BASE))])
        writes = [call for call in self.calls if call[0] == "write"]
        self.assertEqual(
            [int(call[1], 0) for call in writes],
            [
                tool.VENDOR_STAGE,
                tool.ENTRY,
                tool.MAILBOX,
                tool.DDR_BASE,
                tool.DDR_BASE + tool.CHUNK_SIZE,
                tool.DDR_BASE + 2 * tool.CHUNK_SIZE,
            ],
        )
        downloaded = b"".join(self.memory[int(call[1], 0)] for call in writes[3:])
        self.assertEqual(downloaded, self.app)
        self.assertEqual(self.calls[-1], ("exec", hex(tool.DDR_BASE)))
        self.assertEqual(len([call for call in self.calls if call[0] == "version"]), 2)

    def test_bundle_is_snapshotted_before_usb(self):
        original = self.invoke

        def invoke(xfel, *args):
            if args[0] == "version":
                self.binary.write_bytes(b"changed after validation")
                self.helper.write_bytes(b"changed after validation")
                self.vendor.write_bytes(b"changed after validation")
            return original(xfel, *args)

        with patch.object(tool, "invoke", side_effect=invoke):
            tool.run(self.manifest, "test-xfel")
        self.assertEqual(self.memory[tool.ENTRY], self.code)
        self.assertEqual(self.memory[tool.DDR_BASE], self.app[:tool.CHUNK_SIZE])

    def test_dry_run_and_modified_artifacts_never_access_usb(self):
        self.run_tool(dry_run=True)
        self.assertEqual(self.calls, [])
        self.binary.write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "differs from manifest"):
            self.run_tool()
        self.assertEqual(self.calls, [])

    def test_wrong_device_never_writes(self):
        for version in (
            "AWUSBFEX ID=0x00185900(T113) scratchpad=0x00027e00",
            "AWUSBFEX ID=0x00182100(R328) scratchpad=0x28000",
            "",
        ):
            self.version = version
            self.calls.clear()
            with self.assertRaisesRegex(ValueError, "Expected R328"):
                self.run_tool()
            self.assertEqual(self.calls, [("version",)])

    def test_readback_error_never_executes_application(self):
        for address in (
            tool.VENDOR_STAGE, tool.ENTRY, tool.DDR_BASE, tool.DDR_BASE + 2 * tool.CHUNK_SIZE
        ):
            self.calls.clear()
            self.corrupt_address = address
            with self.assertRaisesRegex(ValueError, "readback mismatch"):
                self.run_tool()
            self.assertNotIn(("exec", hex(tool.DDR_BASE)), self.calls)
            if address < tool.DDR_BASE:
                self.assertFalse(any(call[0] == "exec" for call in self.calls))

    def test_init_failure_and_small_capacity_stop_before_ddr_writes(self):
        for status, size in ((1, 0), (3, 0), (4, 0), (5, 128), (2, 64), (2, 0), (2, 256)):
            self.status, self.size_mib = status, size
            self.calls.clear()
            with self.assertRaisesRegex(ValueError, "DRAM loader"):
                self.run_tool()
            self.assertFalse(
                any(call[0] == "write" and int(call[1], 0) >= tool.DDR_BASE for call in self.calls)
            )

    def test_loader_timeout_and_application_timeout_have_different_messages(self):
        for address, message in (
            (tool.ENTRY, "did not return to FEL"), (tool.DDR_BASE, "may be running")
        ):
            self.fail_exec = address
            with self.assertRaisesRegex(ValueError, message):
                self.run_tool()

    def test_manifest_layout_and_path_tampering(self):
        original = self.manifest.read_text()
        for key, value in (("address", 0x20000), ("file", "../outside.bin"), ("sha256", "invalid")):
            report = json.loads(original)
            report["artifacts"]["application"][key] = value
            self.manifest.write_text(json.dumps(report))
            with self.assertRaises(ValueError):
                self.run_tool()
            self.assertEqual(self.calls, [])

    def test_ddr_bounds_and_vectors(self):
        for address, size in (
            (tool.DDR_BASE - 32, tool.DDR_SIZE),
            (tool.DDR_BASE, tool.DDR_SIZE + 1),
            (tool.DDR_BASE, 0),
            (tool.DDR_BASE + 1, 1024),
            (tool.DDR_BASE, len(self.app) - 1),
        ):
            with self.assertRaises(ValueError):
                tool.check_application(self.app, address, size)
        with self.assertRaisesRegex(ValueError, "vectors"):
            tool.check_application(bytes(64), tool.DDR_BASE, tool.DDR_SIZE)
        self.elf.write_bytes(elf_for(self.app, 0x20000))
        with self.assertRaisesRegex(ValueError, "ARM ELF"):
            self.pack()

    def test_vendor_and_elf_mismatch_rejected(self):
        with self.assertRaisesRegex(ValueError, "prebuilt SHA256"):
            tool.prepare_vendor(bytes(32768))
        self.elf.write_bytes(elf_for(bytes(len(self.app)), tool.DDR_BASE))
        with self.assertRaisesRegex(ValueError, "does not match ELF"):
            self.pack()


if __name__ == "__main__":
    with contextlib.redirect_stdout(io.StringIO()):
        unittest.main()
