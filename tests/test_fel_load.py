"""Exercise the FEL loader's ordering and failure paths without USB hardware."""

import importlib.util
import json
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    "fel_load", Path(__file__).resolve().parents[1] / "tools/fel_load.py"
)
tool = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(tool)


class FELLoaderTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        root = Path(self.directory.name)
        self.binary = root / "input.bin"
        self.binary.write_bytes(struct.pack("<8I", *([0xEA000000] * 8)) + bytes(64))
        self.elf = root / "input.elf"
        header = bytearray(52)
        header[:7] = b"\x7fELF\x01\x01\x01"
        struct.pack_into("<H", header, 18, 40)
        struct.pack_into("<I", header, 24, tool.ORIGIN)
        self.elf.write_bytes(header)
        self.manifest = root / "firmware-fel.json"
        self.output = root / "firmware-fel.bin"
        tool.package(self.binary, self.elf, self.output, self.manifest, "PH0_PH1")

    def test_successful_sequence(self):
        commands = []

        def invoke(xfel, *args):
            commands.append(args)
            self.assertEqual(xfel, "test-xfel")
            if args[0] == "version":
                return "AWUSBFEX ID=0x00182100(R328) dflag=0x44 scratchpad=0x00027e00\n"
            if args[0] == "write":
                self.assertEqual(Path(args[2]).read_bytes(), self.binary.read_bytes())
            if args[0] == "read":
                Path(args[3]).write_bytes(self.binary.read_bytes())
            return ""

        with patch.object(tool, "invoke", side_effect=invoke):
            tool.run(self.manifest, "test-xfel")
        self.assertEqual([c[0] for c in commands], ["version", "write", "read", "exec"])
        self.assertEqual(commands[-1], ("exec", "0x28000"))

    def test_wrong_chip_never_writes(self):
        with (
            patch.object(tool, "invoke", return_value="AWUSBFEX ID=0x00185900(T113)") as invoke,
            self.assertRaisesRegex(ValueError, "Expected R328"),
        ):
            tool.run(self.manifest)
        invoke.assert_called_once_with("xfel", "version")

    def test_corrupt_readback_never_executes(self):
        commands = []

        def invoke(xfel, *args):
            commands.append(args[0])
            if args[0] == "version":
                return "AWUSBFEX ID=0x00182100(R328) scratchpad=0x00027e00"
            if args[0] == "read":
                Path(args[3]).write_bytes(bytes(len(self.binary.read_bytes())))
            return ""

        with (
            patch.object(tool, "invoke", side_effect=invoke),
            self.assertRaisesRegex(ValueError, "readback mismatch"),
        ):
            tool.run(self.manifest)
        self.assertEqual(commands, ["version", "write", "read"])

    def test_changed_bin_and_dry_run_do_not_access_usb(self):
        with patch.object(tool, "invoke") as invoke:
            tool.run(self.manifest, dry_run=True)
            self.output.write_bytes(self.output.read_bytes() + b"modified")
            with self.assertRaisesRegex(ValueError, "differs from manifest"):
                tool.run(self.manifest)
        invoke.assert_not_called()

    def test_overlapping_scratchpad_and_missing_address_never_write(self):
        for suffix in (" scratchpad=0x00028000", ""):
            with patch.object(
                tool, "invoke", return_value="AWUSBFEX ID=0x00182100(R328)" + suffix
            ) as invoke:
                with self.assertRaises(ValueError):
                    tool.run(self.manifest)
                invoke.assert_called_once_with("xfel", "version")

    def test_stage_specific_errors(self):
        for stage in ("write", "read", "version"):
            error = subprocess.CalledProcessError(255, ["xfel", stage])
            self.assertIn("NOT", tool.failure_message(error))
        error = subprocess.TimeoutExpired(["xfel", "exec", "0x28000"], 20)
        self.assertIn("may have started", tool.failure_message(error))

    def test_wrong_entry_and_oversize_rejected(self):
        header = bytearray(self.elf.read_bytes())
        struct.pack_into("<I", header, 24, 0x40800000)
        self.elf.write_bytes(header)
        with self.assertRaisesRegex(ValueError, "ELF entry"):
            tool.package(self.binary, self.elf, self.output, self.manifest, "PH0_PH1")
        with self.assertRaisesRegex(ValueError, "exceeds"):
            tool.check_binary(bytes(tool.LENGTH))


class FullSRAMTests(FELLoaderTests):
    def setUp(self):
        super().setUp()
        header = bytearray(self.elf.read_bytes())
        struct.pack_into("<I", header, 24, tool.FULL_ORIGIN)
        self.elf.write_bytes(header)
        self.loader = self.output.parent / "loader-template.bin"
        self.loader_output = self.output.parent / "firmware-fel-loader.bin"
        self.loader.write_bytes(
            bytes(32) + tool.LOADER_FOOTER.pack(b"R328", tool.ORIGIN, tool.FULL_ORIGIN, 0)
        )
        self.pack_full()

    def pack_full(self):
        tool.package(
            self.binary,
            self.elf,
            self.output,
            self.manifest,
            "PH0_PH1",
            "FULL",
            self.loader,
            self.loader_output,
        )

    def test_successful_sequence(self):
        commands = []
        expected = {
            hex(tool.ORIGIN): self.output.read_bytes(),
            hex(tool.LOADER_ADDRESS): self.loader_output.read_bytes(),
        }

        def invoke(xfel, *args):
            commands.append(args)
            self.assertEqual(xfel, "test-xfel")
            if args[0] == "version":
                # Changing build products after validation must not alter USB bytes.
                self.output.write_bytes(b"changed during download")
                self.loader_output.write_bytes(b"changed during download")
                return "AWUSBFEX ID=0x00182100(R328) scratchpad=0x00027e00"
            if args[0] == "write":
                self.assertEqual(Path(args[2]).read_bytes(), expected[args[1]])
            if args[0] == "read":
                Path(args[3]).write_bytes(expected[args[1]])
            return ""

        with patch.object(tool, "invoke", side_effect=invoke):
            tool.run(self.manifest, "test-xfel")
        self.assertEqual(
            [c[0] for c in commands], ["version", "write", "read", "write", "read", "exec"]
        )
        self.assertEqual(commands[-1], ("exec", hex(tool.LOADER_ADDRESS)))
        self.assertNotIn(hex(tool.FULL_ORIGIN), [c[1] for c in commands if len(c) > 1])
        self.assertEqual(
            tool.LOADER_FOOTER.unpack(expected[hex(tool.LOADER_ADDRESS)][-16:]),
            (b"R328", tool.ORIGIN, tool.FULL_ORIGIN, len(self.binary.read_bytes())),
        )

    def test_corrupt_loader_readback_never_executes(self):
        commands = []

        def invoke(xfel, *args):
            commands.append(args[0])
            if args[0] == "version":
                return "AWUSBFEX ID=0x00182100(R328) scratchpad=0x00027e00"
            if args[0] == "read":
                data = (
                    self.output.read_bytes() if args[1] == hex(tool.ORIGIN) else b"corrupt loader"
                )
                Path(args[3]).write_bytes(data)
            return ""

        with (
            patch.object(tool, "invoke", side_effect=invoke),
            self.assertRaisesRegex(ValueError, "readback mismatch"),
        ):
            tool.run(self.manifest)
        self.assertEqual(commands, ["version", "write", "read", "write", "read"])

    def test_staging_boundaries(self):
        vectors = self.binary.read_bytes()[:32]
        tool.check_binary(vectors + bytes(tool.STAGING_LIMIT - 32), "FULL")
        with self.assertRaisesRegex(ValueError, "exceeds"):
            tool.check_binary(vectors + bytes(tool.STAGING_LIMIT - 31), "FULL")

    def test_tampered_staging_and_loader_manifest_never_access_usb(self):
        original = self.manifest.read_text()
        for key, value in (
            ("entry", tool.ORIGIN),
            ("staging_address", tool.FULL_ORIGIN),
            ("loader_address", 0x3FFF0),
            ("relocator_address", 0x3F000),
            ("ram_length", tool.LENGTH),
            ("loader_binary", "../outside.bin"),
            ("loader_sha256", "bad"),
        ):
            report = json.loads(original)
            report[key] = value
            self.manifest.write_text(json.dumps(report))
            with patch.object(tool, "invoke") as invoke:
                with self.assertRaises(ValueError):
                    tool.run(self.manifest)
                invoke.assert_not_called()
        self.manifest.write_text(original)

    def test_bad_loader_parameters_rejected(self):
        for size in (0, 31, 257, 512):
            with self.assertRaisesRegex(ValueError, "loader size"):
                tool.check_loader(bytes(size), 0)
        self.loader.write_bytes(self.loader.read_bytes()[:-4] + struct.pack("<I", 123))
        with self.assertRaisesRegex(ValueError, "loader parameters"):
            self.pack_full()

    def test_wrong_scratchpad_never_writes(self):
        with patch.object(
            tool, "invoke", return_value="AWUSBFEX ID=0x00182100(R328) scratchpad=0x00027c00"
        ) as invoke:
            with self.assertRaisesRegex(ValueError, "Expected R328 scratchpad"):
                tool.run(self.manifest)
            invoke.assert_called_once_with("xfel", "version")

    def test_changed_loader_parameters_rejected_even_with_updated_hash(self):
        data = self.loader_output.read_bytes()[:-4] + struct.pack("<I", 1)
        self.loader_output.write_bytes(data)
        report = json.loads(self.manifest.read_text())
        report["loader_sha256"] = tool.digest(data)
        self.manifest.write_text(json.dumps(report))
        with patch.object(tool, "invoke") as invoke:
            with self.assertRaisesRegex(ValueError, "loader parameters"):
                tool.run(self.manifest)
            invoke.assert_not_called()

    def test_missing_loader_never_access_usb(self):
        self.loader_output.unlink()
        with patch.object(tool, "invoke") as invoke:
            with self.assertRaises(OSError):
                tool.run(self.manifest)
            invoke.assert_not_called()


if __name__ == "__main__":
    unittest.main()
