#!/usr/bin/env python3
"""Pack W25Q64 cold boot: BROM -> boot0 DDR init -> source NOR reader -> app.

The 8 MiB output is a linear SPI NOR programmer image, starting at flash 0.
Only the hash-locked original boot0 prebuilt is required; the NOR reader is
built from boot/nor/boot0_nor.c and replaces the unused NAND reader.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

from nand_image import (
    APP_LENGTH,
    APP_ORIGIN,
    BOOT0_SHA256,
    ROOT,
    TOC_BUFFER,
    check_boot0,
    make_package,
    require,
    sha256,
    stamp,
    validate_elf,
    write_file,
)

PATCH_OFFSET = 0x5564
DISPATCH_OFFSET = 0x60C0
CAPACITY = 0x800000
PACKAGE_OFFSET = 0x10000


def validate_profile(profile: dict) -> None:
    for key, expected in {
        "format": "r328-spinor-direct-v1",
        "model": "W25Q64",
        "capacity": CAPACITY,
        "sector_size": 4096,
        "page_size": 256,
        "package_offset": PACKAGE_OFFSET,
        "spi_clock_hz": 12000000,
        "read_opcode": 3,
        "address_bytes": 3,
        "boot0_sha256": BOOT0_SHA256,
    }.items():
        require(profile.get(key) == expected, f"Unreviewed NOR profile field: {key}")
    require(
        profile.get("ddr", {}).get("base") == hex(APP_ORIGIN)
        and profile["ddr"].get("size") == APP_LENGTH,
        "Unexpected DDR profile",
    )


def configure_boot0(data: bytes, profile: dict, reader: bytes, route: str) -> bytes:
    validate_profile(profile)
    require(sha256(data) == BOOT0_SHA256, "Unrecognized boot0 prebuilt SHA256")
    check_boot0(data)
    require(
        sha256(data[0x38:0xB8]) == profile["ddr"].get("parameters_sha256"),
        "DDR parameter hash mismatch",
    )
    require(0 < len(reader) <= DISPATCH_OFFSET - PATCH_OFFSET, "NOR reader exceeds patch region")
    require(route in ("PH0_PH1", "PB8_PB9"), "Unsupported UART route")
    result = bytearray(data)
    struct.pack_into("<I", result, 0x28, 3)  # BROM SPI NOR medium
    port, tx, rx, mux = (8, 0, 1, 3) if route == "PH0_PH1" else (2, 8, 9, 5)
    for offset, pin in ((0xBC, tx), (0xC4, rx)):
        result[offset : offset + 4] = bytes((port, pin, mux, 1))
    # Fixed Thumb entry (linker KEEP .text.entry at 0x25564). The dispatcher
    # tail-calls the reader, keeping boot0 main's LR and AAPCS stack unchanged.
    result[PATCH_OFFSET:DISPATCH_OFFSET] = reader.ljust(DISPATCH_OFFSET - PATCH_OFFSET, b"\x00")
    struct.pack_into("<HHI", result, DISPATCH_OFFSET, 0x4B00, 0x4718, 0x25565)
    stamp(result, 12)
    check_boot0(result)
    return bytes(result)


def build_image(boot0: bytes, profile: dict, reader: bytes, firmware: bytes, route: str):
    configured = configure_boot0(boot0, profile, reader, route)
    package = make_package(firmware)
    require(len(package) <= CAPACITY - PACKAGE_OFFSET, "Application package exceeds W25Q64")
    image = bytearray(b"\xff" * CAPACITY)
    image[: len(configured)] = configured
    image[PACKAGE_OFFSET : PACKAGE_OFFSET + len(package)] = package
    report = {
        "format": "r328-spinor-linear-v1",
        "handoff": "BROM -> boot0 DDR init -> source NOR reader -> direct bare-metal app",
        "hardware_tested": False,
        "model": profile["model"],
        "image_length": len(image),
        "boot0_offset": 0,
        "boot0_length": len(configured),
        "package_offset": PACKAGE_OFFSET,
        "package_length": len(package),
        "package_buffer": hex(TOC_BUFFER),
        "application_region": {"origin": hex(APP_ORIGIN), "length": APP_LENGTH},
        "boot_uart0_route": route,
        "gic": "SECURE / Group 0",
        "ddr": profile["ddr"],
        "spi": {"controller": 0, "chip_select": 0, "clock_hz": 12000000, "read_opcode": 3},
        "reader_patch": {
            "offset": PATCH_OFFSET,
            "reserved_length": DISPATCH_OFFSET - PATCH_OFFSET,
            "code_length": len(reader),
            "entry": "0x25565 (Thumb)",
            "dispatch_offset": DISPATCH_OFFSET,
        },
        "sha256": {
            "prebuilt_boot0": sha256(boot0),
            "nor_reader": sha256(reader),
            "configured_boot0": sha256(configured),
            "application": sha256(firmware),
            "package": sha256(package),
            "image": sha256(image),
        },
    }
    return bytes(image), package, configured, report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for command in ("pack", "verify"):
        sub = commands.add_parser(command)
        sub.add_argument("--boot0", type=Path, default=ROOT / "prebuilt/r328/boot0-spinand.bin")
        sub.add_argument("--profile", type=Path, default=ROOT / "prebuilt/r328/spinor.json")
        sub.add_argument("--reader", type=Path, required=True)
        sub.add_argument("--reader-elf", type=Path, required=True)
        sub.add_argument("--firmware", type=Path, required=True)
        sub.add_argument("--elf", type=Path, required=True)
        sub.add_argument(
            "--boot-uart0-route",
            "--uart0-route",
            dest="uart0_route",
            default="PH0_PH1",
            choices=("PH0_PH1", "PB8_PB9"),
        )
        sub.add_argument("--output", type=Path, required=True)
        sub.add_argument("--package", type=Path, required=True)
        sub.add_argument("--configured-boot0", type=Path, required=True)
        sub.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()
    inputs = (args.boot0, args.profile, args.reader, args.reader_elf, args.firmware, args.elf)
    outputs = (args.output, args.package, args.configured_boot0, args.manifest)
    require(len({p.resolve() for p in outputs}) == len(outputs), "Output paths must differ")
    require(
        not {p.resolve() for p in inputs} & {p.resolve() for p in outputs},
        "Output overwrites input",
    )
    reader = args.reader.read_bytes()
    reader_elf = bytearray(args.reader_elf.read_bytes())
    require(len(reader_elf) >= 52, "Invalid NOR reader ELF")
    require(struct.unpack_from("<I", reader_elf, 24)[0] == 0x25565, "Wrong NOR reader Thumb entry")
    # Generic ELF validation expects an even ARM entry; normalize only its check
    # input after verifying the exact Thumb entry used by the patched dispatcher.
    struct.pack_into("<I", reader_elf, 24, 0x25564)
    validate_elf(reader_elf, 0x25564, DISPATCH_OFFSET - PATCH_OFFSET, reader)
    firmware = args.firmware.read_bytes()
    validate_elf(args.elf.read_bytes(), APP_ORIGIN, APP_LENGTH, firmware)
    image, package, boot0, report = build_image(
        args.boot0.read_bytes(),
        json.loads(args.profile.read_text()),
        reader,
        firmware,
        args.uart0_route,
    )
    values = (image, package, boot0, (json.dumps(report, indent=2) + "\n").encode())
    for path, value in zip(outputs, values):
        if args.command == "pack":
            write_file(path, value)
        else:
            require(path.read_bytes() == value, f"Output verification failed: {path}")
    print(f"{args.command}: {args.output} ({len(image)} bytes), SHA256 {sha256(image)}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError, struct.error) as error:
        print(f"nor_image: {error}", file=sys.stderr)
        sys.exit(1)
