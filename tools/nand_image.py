#!/usr/bin/env python3
"""Pack direct SPI NAND boot: BROM -> boot0 (DDR/NAND) -> bare-metal application.

The output is physical main-area page data, with boot0's full 2 KiB/page layout.
Programming requires on-die ECC and preserving physical block/bad-block markers.
No vendor system image, U-Boot, secure monitor, DTB or filesystem is required.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APP_ORIGIN = 0x40000000
APP_LENGTH = 0x08000000
TOC_BUFFER = 0x42E00000
TOC_PAYLOAD = 512
CHECKSUM_STAMP = 0x5F0A6C39
BOOT0_SHA256 = "7d67bfbfd681b5fe65a5491bba7c357d33fed362a8f37470e06a6f928c8e38ad"
STORAGE_OFFSET = 0x1F8


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def align(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment


def sum32(data: bytes) -> int:
    padded = data + bytes(-len(data) % 4)
    return sum(word[0] for word in struct.iter_unpack("<I", padded)) & 0xFFFFFFFF


def stamp(data: bytearray, offset: int) -> None:
    struct.pack_into("<I", data, offset, CHECKSUM_STAMP)
    struct.pack_into("<I", data, offset, sum32(data))


def check_stamped(data: bytes, offset: int) -> None:
    require(len(data) % 4 == 0 and offset + 4 <= len(data), "Invalid stamped block length")
    expected = (sum32(data) - u32(data, offset) + CHECKSUM_STAMP) & 0xFFFFFFFF
    require(expected == u32(data, offset), "Boot block checksum mismatch")


def validate_profile(profile: dict) -> None:
    # This profile is checked against the supplied boot0's ABI, not an arbitrary
    # NAND geometry description. A different flash requires reviewing its driver.
    require(profile.get("format") == "r328-spinand-direct-v1", "Unsupported NAND profile")
    devices = {
        "FS35ND04G-S2Y2": (4096, [0xCD, 0xEC, 0x11, 255, 255, 255, 255, 255]),
        "MX35LF1GE4AB": (1024, [0xC2, 0x12, 255, 255, 255, 255, 255, 255]),
    }
    require(profile.get("model") in devices, "Unreviewed NAND profile field: model")
    blocks, chip_id = devices[profile["model"]]
    for key, value in {
        "page_size": 2048,
        "oob_size": 64,
        "pages_per_block": 64,
        "blocks_per_die": blocks,
        "chip_id": chip_id,
        "boot0_data_per_page": 2048,
        "boot0_blocks": [0, 2, 4, 6],
        "package_first_block": 8,
        "package_last_block": 31,
        "spi_operation_options": 0,
        "spi_clock_mhz": 30,
        "boot0_sha256": BOOT0_SHA256,
    }.items():
        require(profile.get(key) == value, f"Unreviewed NAND profile field: {key}")
    require(
        profile.get("ddr", {}).get("size") == APP_LENGTH
        and profile["ddr"].get("base") == hex(APP_ORIGIN),
        "Unexpected DDR profile",
    )


def check_boot0(data: bytes) -> None:
    require(
        len(data) == 32768
        and data[4:12] == b"eGON.BT0"
        and u32(data, 16) == len(data)
        and u32(data, 20) == 48
        and u32(data, 0x20) == 0x20000,
        "Unsupported boot0 header",
    )
    check_stamped(data, 12)


def configure_boot0(data: bytes, profile: dict, route: str) -> bytes:
    validate_profile(profile)
    require(sha256(data) == BOOT0_SHA256, "Unrecognized boot0 prebuilt SHA256")
    check_boot0(data)
    require(
        sha256(data[0x38:0xB8]) == profile["ddr"].get("parameters_sha256"),
        "DDR parameter hash mismatch",
    )
    require(data[STORAGE_OFFSET : STORAGE_OFFSET + 96] == bytes(96), "Unexpected storage data")
    require(route in ("PH0_PH1", "PB8_PB9"), "Unsupported UART route")
    result = bytearray(data)
    # boot0 reads the medium nibble at +0x28: 4 selects SPI NAND, 0 raw NAND.
    struct.pack_into("<I", result, 0x28, 4)
    # Packed storage ABI actually consumed by this boot0 (base + 0x1f8).
    storage = bytearray(96)
    storage[:6] = bytes([1, 1, 1, 1, 1, 4])
    struct.pack_into("<H", storage, 6, 1)
    for offset, value in {
        8: profile["pages_per_block"],
        12: profile["blocks_per_die"],
        16: profile["spi_operation_options"],
        20: profile["spi_clock_mhz"],
        40: 1,
        44: 100000,
        48: 4,
        56: profile["package_first_block"],
        60: profile["package_last_block"],
        64: profile["package_last_block"] + 1,
    }.items():
        struct.pack_into("<I", storage, offset, value)
    storage[28:36] = bytes(profile["chip_id"])
    result[STORAGE_OFFSET : STORAGE_OFFSET + 96] = storage
    # GPIO records: port, pin, mux, pull, drive, data, two reserved bytes.
    port, tx, rx, mux = (8, 0, 1, 3) if route == "PH0_PH1" else (2, 8, 9, 5)
    for offset, pin in ((0xBC, tx), (0xC4, rx)):
        result[offset : offset + 4] = bytes((port, pin, mux, 1))
    stamp(result, 12)
    check_boot0(result)
    return bytes(result)


def validate_elf(data: bytes, origin: int, length: int, firmware: bytes | None = None) -> None:
    require(
        len(data) >= 52
        and data[:7] == b"\x7fELF\x01\x01\x01"
        and struct.unpack_from("<H", data, 18)[0] == 40
        and u32(data, 24) == origin,
        f"Expected ARM ELF linked at 0x{origin:08x}; rebuild for Flash boot",
    )
    offset = u32(data, 28)
    stride, count = struct.unpack_from("<HH", data, 42)
    require(
        stride == 32 and count > 0 and offset + count * stride <= len(data),
        "Invalid ELF program headers",
    )
    segments = []
    for index in range(count):
        kind, file_offset, virtual, physical, filesz, memsz, _, _ = struct.unpack_from(
            "<8I", data, offset + index * stride
        )
        if kind == 1:
            require(
                filesz <= memsz
                and virtual == physical
                and origin <= virtual <= virtual + memsz <= origin + length,
                "ELF load segment exceeds the Flash application region",
            )
            require(file_offset + filesz <= len(data), "ELF load segment exceeds the file")
            if filesz:
                segments.append((physical - origin, data[file_offset : file_offset + filesz]))
    require(bool(segments), "ELF has no loadable data")
    if firmware is not None:
        expected = bytearray(max(offset + len(segment) for offset, segment in segments))
        for offset, segment in segments:
            expected[offset : offset + len(segment)] = segment
        require(firmware == expected, "Firmware BIN does not match ELF load segments")


def validate_application(firmware: bytes) -> None:
    require(
        512 < len(firmware) <= TOC_BUFFER - APP_ORIGIN, "Application overlaps TOC staging buffer"
    )
    require(
        u32(firmware, 0x2C) == APP_ORIGIN,
        "Missing Flash boot header at +0x2c; rebuild for Flash boot",
    )
    for index, word in enumerate(struct.unpack_from("<8I", firmware)):
        require(word >> 24 == 0xEA, "Expected eight ARM branch vectors")
        displacement = word & 0xFFFFFF
        if displacement & 0x800000:
            displacement -= 1 << 24
        require(
            512 <= index * 4 + 8 + displacement * 4 < len(firmware),
            "ARM vector target outside application code",
        )


def make_package(firmware: bytes) -> bytes:
    validate_application(firmware)
    length = align(TOC_PAYLOAD + len(firmware), 4096)
    result = bytearray(length)
    result[:16] = b"sunxi-package\0\0\0"
    struct.pack_into("<I", result, 16, 0x89119800)
    struct.pack_into("<II", result, 32, 1, length)
    struct.pack_into("<I", result, 60, 0x3B45494D)
    # boot0's "freertos" branch bypasses U-Boot header monkey-patching and
    # monitor/OP-TEE dispatch. This is just a key, not a linked RTOS.
    result[64 : 64 + 8] = b"freertos"
    struct.pack_into("<6I", result, 128, TOC_PAYLOAD, len(firmware), 0, 3, APP_ORIGIN, 0)
    struct.pack_into("<I", result, 64 + 364, 0x3B454949)
    result[TOC_PAYLOAD : TOC_PAYLOAD + len(firmware)] = firmware
    stamp(result, 20)
    check_stamped(result, 20)
    return bytes(result)


def build_image(
    boot0: bytes, profile: dict, firmware: bytes, route: str = "PH0_PH1"
) -> tuple[bytes, bytes, bytes, dict]:
    configured = configure_boot0(boot0, profile, route)
    package = make_package(firmware)
    block_size = profile["page_size"] * profile["pages_per_block"]
    first, last = profile["package_first_block"], profile["package_last_block"]
    span = align(len(package), block_size) // block_size
    require(span <= last - first + 1, "Application package exceeds NAND boot area")
    # Bound the DDR copy too, independently of today's smaller NAND boot area.
    require(TOC_PAYLOAD + align(len(firmware), 512) <= len(package), "Invalid sector padding")
    output = bytearray(b"\xff" * ((last + 1) * block_size))
    for block in profile["boot0_blocks"]:
        for i in range(0, len(configured), profile["boot0_data_per_page"]):
            page = i // profile["boot0_data_per_page"]
            start = block * block_size + page * profile["page_size"]
            count = profile["boot0_data_per_page"]
            output[start : start + count] = configured[i : i + count]
    copies = list(range(first, last - span + 2, span))
    for block in copies:
        start = block * block_size
        output[start : start + len(package)] = package
    report = {
        "format": "r328-spinand-physical-main-area-v1",
        "handoff": "BROM -> boot0 (DDR init, NAND read, copy) -> bare metal",
        "hardware_tested": False,
        "gic": "SECURE / Group 0",
        "application_uart0_route": route,
        "application_region": {"origin": hex(APP_ORIGIN), "length": APP_LENGTH},
        "ddr": profile["ddr"],
        "nand": {
            key: profile[key]
            for key in (
                "model",
                "page_size",
                "oob_size",
                "pages_per_block",
                "blocks_per_die",
                "boot0_data_per_page",
                "boot0_blocks",
                "package_first_block",
                "package_last_block",
            )
        },
        "package_copies": [{"block": b, "offset": b * block_size} for b in copies],
        "package_blocks_per_copy": span,
        "package_length": len(package),
        "package_item": "freertos (vendor direct-application dispatch key)",
        "package_buffer": hex(TOC_BUFFER),
        "programming": {
            "on_die_ecc": True,
            "oob_included": False,
            "preserve_factory_bad_block_markers": True,
            "addressing": "physical; skip bad blocks without compacting image offsets",
        },
        "sha256": {
            "prebuilt_boot0": sha256(boot0),
            "configured_boot0": sha256(configured),
            "application": sha256(firmware),
            "package": sha256(package),
            "image": sha256(output),
        },
        "image_length": len(output),
    }
    return bytes(output), package, configured, report


def write_file(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_bytes(data)
    temporary.replace(path)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for command in ("pack", "verify"):
        sub = commands.add_parser(command)
        sub.add_argument("--boot0", type=Path, default=ROOT / "prebuilt/r328/boot0-spinand.bin")
        sub.add_argument("--profile", type=Path, default=ROOT / "prebuilt/r328/spinand.json")
        sub.add_argument("--firmware", type=Path, required=True)
        sub.add_argument("--elf", type=Path, required=True)
        sub.add_argument("--uart0-route", default="PH0_PH1", choices=("PH0_PH1", "PB8_PB9"))
        sub.add_argument("--output", type=Path, required=True)
        sub.add_argument("--package", type=Path, required=True)
        sub.add_argument("--configured-boot0", type=Path, required=True)
        sub.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()
    inputs = (args.boot0, args.profile, args.firmware, args.elf)
    outputs = (args.output, args.package, args.configured_boot0, args.manifest)
    require(len({p.resolve() for p in outputs}) == len(outputs), "Output paths must differ")
    require(
        not {p.resolve() for p in inputs} & {p.resolve() for p in outputs},
        "Output overwrites input",
    )
    firmware = args.firmware.read_bytes()
    validate_elf(args.elf.read_bytes(), APP_ORIGIN, APP_LENGTH, firmware)
    image, package, boot0, report = build_image(
        args.boot0.read_bytes(), json.loads(args.profile.read_text()), firmware, args.uart0_route
    )
    values = (image, package, boot0, (json.dumps(report, indent=2) + "\n").encode())
    for path, data in zip(outputs, values):
        if args.command == "pack":
            write_file(path, data)
        else:
            require(path.read_bytes() == data, f"Output verification failed: {path}")
    print(f"{args.command}: {args.output} ({len(image)} bytes), SHA256 {sha256(image)}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError, struct.error) as error:
        print(f"nand_image: {error}", file=sys.stderr)
        sys.exit(1)
