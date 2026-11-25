#!/usr/bin/env python3
"""Program the MX35LF1GE4AB boot area using stock xfel and a returning SRAM helper."""

from __future__ import annotations

import argparse
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import fel_nor as transport
import nand_image
from fel_nor import BUFFER, BUFFER_SIZE, ENTRY, ERASE, MAILBOX, PROGRAM, READ
from nand_image import ROOT, require, sha256, u32

MAGIC = 0x444E4146
FOOTER = struct.pack(
    "<8s8I",
    b"R328NAN1",
    ENTRY,
    MAILBOX,
    BUFFER,
    BUFFER_SIZE,
    transport.STACK_BOTTOM,
    transport.STACK_TOP,
    MAGIC,
    1,
)
PAGE_SIZE = 2048
BLOCK_SIZE = 0x20000
IMAGE_SIZE = 0x400000
BAD_BLOCK = 5
ERRORS = {
    **transport.ERRORS,
    6: "expected MX35LF1GE4AB JEDEC C2 12 (9F + dummy)",
    10: "uncorrectable or reserved ECC status; detail is physical page number",
    11: "factory bad block; detail contains both spare markers",
    12: "NAND erase failure (E_FAIL)",
    13: "NAND program failure (P_FAIL)",
    14: "feature readback failed or OTP access mode active",
}


class Session(transport.Session):
    magic = MAGIC
    errors = ERRORS
    medium = "NAND"
    accepted_ids = (0xC212,)
    description = "MX35LF1GE4AB, 128 MiB, page 2048+64 / block 128 KiB"


def load_image(image, manifest):
    """Reconstruct all physical copies before allowing any USB operation."""
    report = json.loads(manifest.read_text())
    require(
        report.get("format") == "r328-spinand-physical-main-area-v1"
        and report.get("nand", {}).get("model") == "MX35LF1GE4AB"
        and report.get("image_length") == IMAGE_SIZE,
        "Expected MX35LF1GE4AB physical main-area image (4 MiB); rebuild nand-debug",
    )
    data = image.read_bytes()
    require(
        len(data) == IMAGE_SIZE and sha256(data) == report["sha256"]["image"],
        "NAND image differs from manifest",
    )
    start = 8 * BLOCK_SIZE
    length = u32(data, start + 132)  # payload size of the single TOC1 item
    require(512 < length <= 24 * BLOCK_SIZE - 512, "Invalid NAND application size")
    firmware = data[start + 512 : start + 512 + length]
    profile = json.loads((ROOT / "prebuilt/r328/spinand.json").read_text())
    expected, _, _, expected_report = nand_image.build_image(
        (ROOT / "prebuilt/r328/boot0-spinand.bin").read_bytes(),
        profile,
        firmware,
        report["application_uart0_route"],
    )
    require(data == expected and report == expected_report, "NAND layout or manifest mismatch")
    return data, report


def scan(session):
    bad = set()
    print("Scanning factory markers in both first pages of physical blocks 0–31...", flush=True)
    for block in range(32):
        markers, _ = session.command(BAD_BLOCK, block * BLOCK_SIZE)
        require(0 <= markers <= 0xFFFF, "Invalid bad-block marker response")
        if markers != 0xFFFF:
            bad.add(block)
    print(f"Bad blocks in boot area: {sorted(bad) or 'none'}", flush=True)
    return bad


def viable_copies(report, bad):
    boot0 = [block for block in report["nand"]["boot0_blocks"] if block not in bad]
    span = report["package_blocks_per_copy"]
    packages = [
        item["block"]
        for item in report["package_copies"]
        if not bad.intersection(range(item["block"], item["block"] + span))
    ]
    require(
        boot0 and packages,
        "No complete boot0/application copy survives bad blocks; erase cancelled",
    )
    print(f"Usable copies: boot0 {len(boot0)}, application {len(packages)}", flush=True)


def verify(session, data, bad):
    checked = 0
    for address in range(0, len(data), BUFFER_SIZE):
        if address // BLOCK_SIZE in bad:
            continue
        expected = data[address : address + BUFFER_SIZE]
        session.command(READ, address, len(expected))
        actual = session.read(BUFFER, len(expected))
        if actual != expected:
            index = next(i for i, pair in enumerate(zip(actual, expected)) if pair[0] != pair[1])
            raise ValueError(f"NAND verify mismatch at physical 0x{address + index:06x}")
        checked += len(expected)
        if (address + len(expected)) % BLOCK_SIZE == 0:
            print(f"Verify physical block {address // BLOCK_SIZE}/31", flush=True)
    print(
        f"NAND readback PASS: {checked} bytes, skipped bad blocks {sorted(bad)}; "
        "factory markers preserved, internal ECC enabled.",
        flush=True,
    )


def run(command, helper_manifest, xfel="xfel", image=None, manifest=None, dry_run=False):
    require(command in ("probe", "flash", "verify"), "Unknown command")
    helper = transport.load_helper(helper_manifest, footer=FOOTER, kind="nand")
    data = report = None
    if command != "probe":
        require(image is not None and manifest is not None, "Image and manifest required")
        data, report = load_image(image, manifest)
    if dry_run:
        print(f"FEL NAND {command} inputs OK: helper {len(helper)} bytes; no USB access")
        return
    changed = False
    try:
        with tempfile.TemporaryDirectory(prefix="r328-fel-nand-") as directory:
            session = Session(xfel, directory)
            session.connect(helper)
            bad = scan(session)
            if command == "probe":
                return
            viable_copies(report, bad)
            if command == "flash":
                print("Erasing/programming good physical blocks in first 4 MiB only...", flush=True)
                for block in range(32):
                    if block in bad:
                        continue
                    base = block * BLOCK_SIZE
                    changed = True  # exec can have erased even if the USB response fails.
                    session.command(ERASE, base, BLOCK_SIZE)
                    for address in range(base, base + BLOCK_SIZE, BUFFER_SIZE):
                        chunk = data[address : address + BUFFER_SIZE]
                        if chunk != b"\xff" * len(chunk):
                            session.command(PROGRAM, address, len(chunk), chunk)
                    print(f"Program physical block {block}/31", flush=True)
            verify(session, data, bad)
            # Detect marker damage or a new bad block before claiming success.
            require(scan(session) == bad, "Bad-block markers changed during operation")
            if changed:
                print("Flash complete. Release FEL and reset/power-cycle to cold boot.", flush=True)
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired):
        if changed:
            print(
                "NAND may be partially programmed. No automatic retry/remapping or bad-block "
                "marking was performed; inspect the failure before retrying.",
                file=sys.stderr,
            )
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    item = sub.add_parser("package")
    item.add_argument("--binary", type=Path, required=True)
    item.add_argument("--elf", type=Path, required=True)
    item.add_argument("--manifest", type=Path, required=True)
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
        transport.package(args.binary, args.elf, args.manifest, footer=FOOTER, kind="nand")
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
        print(f"fel_nand: {error}", file=sys.stderr)
        sys.exit(1)
