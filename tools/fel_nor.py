#!/usr/bin/env python3
"""Program W25Q64 using stock xfel read/write/exec and a returning SRAM helper."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shlex
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

from nand_image import check_boot0, check_stamped, require, validate_elf, write_file

ENTRY = 0x28000
MAILBOX = 0x29000
BUFFER = 0x2A000
BUFFER_SIZE = 0x4000
STACK_BOTTOM = 0x2F000
STACK_TOP = 0x30000
MAGIC = 0x524F4E46
CHIP_ID = 0x00182100
CAPACITY = 0x800000
ERASE_SIZE = 0x10000
FOOTER = struct.pack(
    "<8s8I", b"R328NOR1", ENTRY, MAILBOX, BUFFER, BUFFER_SIZE, STACK_BOTTOM, STACK_TOP, MAGIC, 1
)
MESSAGE = struct.Struct("<9I")
PROBE, READ, ERASE, PROGRAM = 1, 2, 3, 4
ERRORS = {
    1: "mailbox ABI mismatch",
    2: "unsupported operation",
    3: "address/length out of range",
    4: "SPI FIFO error or timeout",
    5: "Flash BUSY timeout",
    6: "expected W25Q64 JEDEC EF4017 or EF7017",
    7: "Flash write protection / WEL rejected",
    8: "SRAM command/data CRC mismatch",
    9: "CPU0 with MMU/D-cache off required",
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def check_helper(data, footer=FOOTER, medium="NOR"):
    require(64 <= len(data) <= MAILBOX - ENTRY, f"FEL {medium} helper exceeds code window")
    require(data[-len(footer) :] == footer, f"Unexpected FEL {medium} helper ABI footer")


def package(binary, elf, manifest, *, footer=FOOTER, kind="nor"):
    require(manifest.resolve() not in (binary.resolve(), elf.resolve()), "Output overwrites input")
    require(
        binary.resolve().parent == manifest.resolve().parent, "Keep helper and manifest together"
    )
    require(re.fullmatch(r"[A-Za-z0-9_-]+\.bin", binary.name), "Invalid helper filename")
    data = binary.read_bytes()
    check_helper(data, footer, kind.upper())
    validate_elf(elf.read_bytes(), ENTRY, MAILBOX - ENTRY, data)
    report = {
        "format": f"r328-fel-{kind}-helper-v1",
        "binary": binary.name,
        "entry": ENTRY,
        "mailbox": MAILBOX,
        "buffer": BUFFER,
        "buffer_size": BUFFER_SIZE,
        "stack_bottom": STACK_BOTTOM,
        "stack_top": STACK_TOP,
        "chip_id": CHIP_ID,
        "size": len(data),
        "sha256": digest(data),
    }
    write_file(manifest, (json.dumps(report, indent=2) + "\n").encode())
    print(f"FEL {kind.upper()} helper: {len(data)} bytes, entry 0x{ENTRY:x}")


def load_helper(manifest, *, footer=FOOTER, kind="nor"):
    report = json.loads(manifest.read_text())
    for key, value in {
        "format": f"r328-fel-{kind}-helper-v1",
        "entry": ENTRY,
        "mailbox": MAILBOX,
        "buffer": BUFFER,
        "buffer_size": BUFFER_SIZE,
        "stack_bottom": STACK_BOTTOM,
        "stack_top": STACK_TOP,
        "chip_id": CHIP_ID,
    }.items():
        require(report.get(key) == value, f"Unexpected helper layout: {key}")
    name = report["binary"]
    require(
        isinstance(name, str) and re.fullmatch(r"[A-Za-z0-9_-]+\.bin", name),
        "Invalid helper filename",
    )
    data = (manifest.resolve().parent / name).read_bytes()
    require(
        len(data) == report["size"] and digest(data) == report["sha256"],
        "Helper differs from manifest",
    )
    check_helper(data, footer, kind.upper())
    return data


def load_image(image, manifest):
    report = json.loads(manifest.read_text())
    require(
        report.get("format") == "r328-spinor-linear-v1"
        and report.get("model") == "W25Q64"
        and report.get("image_length") == CAPACITY
        and report.get("boot0_offset") == 0
        and report.get("boot0_length") == 32768
        and report.get("package_offset") == ERASE_SIZE,
        "Expected complete W25Q64 NOR image manifest",
    )
    data = image.read_bytes()
    require(
        len(data) == CAPACITY and digest(data) == report["sha256"]["image"],
        "NOR image differs from manifest",
    )
    check_boot0(data[:32768])
    length = report["package_length"]
    require(
        isinstance(length, int) and 4096 <= length <= CAPACITY - ERASE_SIZE and length % 4096 == 0,
        "Invalid TOC length",
    )
    check_stamped(data[ERASE_SIZE : ERASE_SIZE + length], 20)
    return data


def invoke(xfel, *args):
    command = [xfel, *args]
    result = subprocess.run(command, check=False, capture_output=True, text=True, timeout=20)
    if result.returncode != 0 or "ERROR:" in result.stdout:
        raise RuntimeError(f"{shlex.join(command)} failed: {result.stdout}{result.stderr}")
    return result.stdout


class Session:
    magic = MAGIC
    errors = ERRORS
    medium = "NOR"
    accepted_ids = (0xEF4017, 0xEF7017)
    description = "W25Q64, 8 MiB"

    def invoke(self, *args):
        return invoke(self.xfel, *args)

    def __init__(self, xfel, directory):
        self.xfel = xfel
        self.directory = Path(directory)
        self.sequence = 0

    def write(self, address, data):
        path = self.directory / "upload.bin"
        path.write_bytes(data)
        self.invoke("write", hex(address), str(path))

    def read(self, address, length):
        path = self.directory / "readback.bin"
        path.unlink(missing_ok=True)
        self.invoke("read", hex(address), str(length), str(path))
        data = path.read_bytes()
        require(len(data) == length, "Short FEL read")
        return data

    def connect(self, helper):
        print(
            "Release the FEL button immediately after entering BROM FEL; "
            "keep it released during Flash operations.",
            flush=True,
        )
        version = self.invoke("version")
        chip = re.search(r"AWUSBFEX\s+ID=0x([0-9a-fA-F]+)\b", version)
        scratch = re.search(r"scratchpad=0x([0-9a-fA-F]+)\b", version)
        require(
            chip is not None and int(chip.group(1), 16) == CHIP_ID,
            "Expected R328 BROM FEL ID 0x00182100; nothing downloaded",
        )
        require(
            scratch is not None and int(scratch.group(1), 16) == 0x27E00,
            "Expected BROM scratchpad 0x27e00; nothing downloaded",
        )
        self.write(ENTRY, helper)
        require(
            self.read(ENTRY, len(helper)) == helper,
            "Helper SRAM readback mismatch; execution cancelled",
        )
        detail, _ = self.command(PROBE)
        require(detail in self.accepted_ids, f"Unexpected {self.medium} JEDEC ID {detail:06x}")
        print(f"R328 FEL, {self.description}, JEDEC {detail:06X}, SPI0 / CS0", flush=True)

    def command(self, operation, address=0, length=0, payload=b""):
        self.sequence += 1
        header = struct.pack("<5I", self.magic, self.sequence, operation, address, length)
        require(
            len(payload) == length if operation == PROGRAM else not payload,
            "Invalid upload length",
        )
        crc = zlib.crc32(payload, zlib.crc32(header))
        request = MESSAGE.pack(
            self.magic, self.sequence, operation, address, length, crc, 0xFFFFFFFF, 0, 0
        )
        if payload:
            self.write(BUFFER, payload)
        self.write(MAILBOX, request)
        self.invoke("exec", hex(ENTRY))
        response = MESSAGE.unpack(self.read(MAILBOX, MESSAGE.size))
        require(response[:6] == MESSAGE.unpack(request)[:6], "FEL mailbox echo mismatch")
        status, detail, completed = response[6:]
        require(
            status == 0,
            f"{self.medium} operation {operation} at 0x{address:06x}: {self.errors.get(status, f'unknown helper status {status}')} (detail 0x{detail:06x}, completed {completed})",
        )
        require(
            completed == length,
            f"Incomplete {self.medium} operation at 0x{address:06x}: {completed}/{length}",
        )
        return detail, completed


def verify(session, image):
    for address in range(0, CAPACITY, BUFFER_SIZE):
        expected = image[address : address + BUFFER_SIZE]
        session.command(READ, address, len(expected))
        actual = session.read(BUFFER, len(expected))
        if actual != expected:
            index = next(i for i, (a, b) in enumerate(zip(actual, expected)) if a != b)
            raise ValueError(
                f"NOR verify mismatch at 0x{address + index:06x}: expected {expected[index]:02x}, read {actual[index]:02x}"
            )
        if (address + len(expected)) % 0x100000 == 0:
            print(f"Verify {(address + len(expected)) // 0x100000}/8 MiB", flush=True)
    print(f"Full NOR readback PASS, SHA256 {digest(image)}", flush=True)


def run(command, helper_manifest, xfel="xfel", image=None, manifest=None, dry_run=False):
    require(command in ("probe", "flash", "verify"), "Unknown command")
    helper = load_helper(helper_manifest)
    data = None
    if command != "probe":
        require(image is not None and manifest is not None, "Image and manifest required")
        data = load_image(image, manifest)
    if dry_run:
        print(
            f"FEL NOR {command} inputs OK: helper {len(helper)} bytes, SRAM 0x28000–0x2ffff; no USB access"
        )
        return
    changed = False
    try:
        with tempfile.TemporaryDirectory(prefix="r328-fel-nor-") as directory:
            session = Session(xfel, directory)
            session.connect(helper)
            if command == "probe":
                return
            if command == "flash":
                print("Erasing complete W25Q64 (8 MiB)...", flush=True)
                for address in range(0, CAPACITY, ERASE_SIZE):
                    changed = True  # An exec failure may still have started erasing.
                    session.command(ERASE, address, ERASE_SIZE)
                    if (address + ERASE_SIZE) % 0x100000 == 0:
                        print(f"Erase {(address + ERASE_SIZE) // 0x100000}/8 MiB", flush=True)
                programmed = 0
                for address in range(0, CAPACITY, BUFFER_SIZE):
                    block = data[address : address + BUFFER_SIZE]
                    if block != b"\xff" * len(block):
                        session.command(PROGRAM, address, len(block), block)
                        programmed += len(block)
                        if (address + len(block)) % 0x100000 == 0:
                            print(f"Programmed through 0x{address + len(block):06x}", flush=True)
                print(
                    f"Programmed {programmed} bytes; checking all 8 MiB including erased gaps.",
                    flush=True,
                )
            verify(session, data)
            if changed:
                print(
                    "Flash complete. Keep the FEL button released and reset/power-cycle to cold boot.",
                    flush=True,
                )
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired):
        if changed:
            print(
                "Flash may be partially erased/programmed. Fix the error and retry the complete flash; no automatic retry was performed.",
                file=sys.stderr,
            )
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    package_parser = sub.add_parser("package")
    package_parser.add_argument("--binary", type=Path, required=True)
    package_parser.add_argument("--elf", type=Path, required=True)
    package_parser.add_argument("--manifest", type=Path, required=True)
    for command in ("probe", "flash", "verify"):
        item = sub.add_parser(command)
        item.add_argument("--helper-manifest", type=Path, required=True)
        item.add_argument("--xfel", default="xfel")
        item.add_argument("--dry-run", action="store_true")
        if command != "probe":
            item.add_argument("--image", type=Path, required=True)
            item.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "package":
        package(args.binary, args.elf, args.manifest)
    else:
        run(
            args.command,
            args.helper_manifest,
            args.xfel,
            getattr(args, "image", None),
            getattr(args, "manifest", None),
            args.dry_run,
        )


if __name__ == "__main__":
    try:
        main()
    except (
        OSError,
        ValueError,
        RuntimeError,
        KeyError,
        TypeError,
        struct.error,
        subprocess.TimeoutExpired,
    ) as error:
        print(f"fel_nor: {error}", file=sys.stderr)
        sys.exit(1)
