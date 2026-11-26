#!/usr/bin/env python3
"""Initialize R328 DRAM, return to BROM FEL, then load/verify/execute the application.

Only xfel version/read/write/exec are used. No Flash commands or boot0 main.
"""

from __future__ import annotations

import argparse
import json
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from fel_load import invoke
from nand_image import BOOT0_SHA256, check_boot0, require, sha256, validate_elf, write_file

DDR_BASE = 0x40000000
DDR_SIZE = 0x08000000
ENTRY = 0x2C000
MAILBOX = 0x2CF00
VENDOR_STAGE = 0x28000
VENDOR_SIZE = 0x3100
VENDOR_BASE = 0x20000
VENDOR_INIT = 0x22DAD
BACKUP = 0x2D000
BACKUP_SIZE = 0x8000
STACK_BOTTOM = 0x35000
STACK_TOP = 0x38000
MAGIC = 0x52444446
ABI = 1
CHIP_ID = 0x00182100
SCRATCHPAD = 0x27E00
CHUNK_SIZE = 0x40000
VENDOR_SHA256 = "f685c6ce9414b9d43b23417e612e28c4e22a1a5b37e95de99e53fb9c2257ae06"
FOOTER = struct.pack(
    "<8s12I",
    b"R328DDR1",
    ENTRY,
    MAILBOX,
    VENDOR_STAGE,
    VENDOR_SIZE,
    VENDOR_BASE,
    BACKUP,
    BACKUP_SIZE,
    STACK_BOTTOM,
    STACK_TOP,
    VENDOR_INIT,
    MAGIC,
    ABI,
)
MESSAGE = struct.Struct("<4I")
ERRORS = {
    1: "DRAM initialization did not finish",
    3: "vendor DRAM initialization failed",
    4: "CPU0 with MMU/D-cache disabled required",
    5: "unsupported BROM stack address or loader stack overflow",
}


def prepare_vendor(data):
    require(sha256(data) == BOOT0_SHA256, "Unrecognized boot0 prebuilt SHA256")
    check_boot0(data)
    # Includes parameters, arithmetic/delay helpers, DRAM training and chip check.
    # All referenced strings outside this prefix are used only by printf.
    result = bytearray(data[:VENDOR_SIZE])
    result[0x12AC:0x12B0] = b"\x00\x20\x70\x47"  # Thumb: movs r0, #0; bx lr
    require(sha256(result) == VENDOR_SHA256, "Unexpected DRAM code extraction")
    return bytes(result)


def check_helper(data):
    require(64 <= len(data) <= MAILBOX - ENTRY, "DRAM loader exceeds code window")
    require(data[-len(FOOTER) :] == FOOTER, "Unexpected DRAM loader ABI footer")


def check_application(data, origin, length):
    require(
        type(origin) is int
        and type(length) is int
        and origin % 32 == 0
        and DDR_BASE <= origin < origin + length <= DDR_BASE + DDR_SIZE,
        "Application region must fit the 128 MiB DDR window",
    )
    require(32 <= len(data) <= length, "Application BIN exceeds DDR region")
    for index, (word,) in enumerate(struct.iter_unpack("<I", data[:32])):
        require(word >> 24 == 0xEA, "Expected eight raw ARM branch vectors")
        displacement = word & 0xFFFFFF
        if displacement & 0x800000:
            displacement -= 1 << 24
        require(
            32 <= index * 4 + 8 + displacement * 4 < len(data),
            "ARM vector target outside application code",
        )


def artifact_name(name):
    require(
        isinstance(name, str) and re.fullmatch(r"[A-Za-z0-9_-]+\.bin", name),
        "Invalid artifact filename",
    )
    return name


def package(binary, elf, helper, helper_elf, boot0, vendor_output, manifest, origin, length):
    inputs = [binary, elf, helper, helper_elf, boot0]
    outputs = [vendor_output, manifest]
    require(
        len({path.resolve() for path in outputs}) == len(outputs)
        and not {path.resolve() for path in inputs} & {path.resolve() for path in outputs},
        "Outputs must differ from inputs",
    )
    require(
        all(
            path.resolve().parent == manifest.resolve().parent
            for path in (binary, helper, vendor_output)
        ),
        "Keep manifest and binaries in the same directory",
    )
    app = binary.read_bytes()
    code = helper.read_bytes()
    vendor = prepare_vendor(boot0.read_bytes())
    check_application(app, origin, length)
    validate_elf(elf.read_bytes(), origin, length, app)
    check_helper(code)
    validate_elf(helper_elf.read_bytes(), ENTRY, MAILBOX - ENTRY, code)
    artifacts = {}
    for key, path, address, data in (
        ("application", binary, origin, app),
        ("loader", helper, ENTRY, code),
        ("vendor", vendor_output, VENDOR_STAGE, vendor),
    ):
        artifacts[key] = {
            "file": artifact_name(path.name),
            "address": address,
            "size": len(data),
            "sha256": sha256(data),
        }
    require(len({item["file"] for item in artifacts.values()}) == 3, "Duplicate artifact names")
    report = {
        "format": "r328-fel-ddr-v1",
        "origin": origin,
        "length": length,
        "chip_id": CHIP_ID,
        "scratchpad": SCRATCHPAD,
        "boot0_sha256": BOOT0_SHA256,
        "artifacts": artifacts,
    }
    write_file(vendor_output, vendor)
    write_file(manifest, (json.dumps(report, indent=2) + "\n").encode())
    print(
        f"FEL DDR: {len(app)} application bytes at 0x{origin:08x}, "
        f"{length // 1024**2} MiB window"
    )


def load_bundle(manifest):
    report = json.loads(manifest.read_text())
    for key, expected in {
        "format": "r328-fel-ddr-v1",
        "chip_id": CHIP_ID,
        "scratchpad": SCRATCHPAD,
        "boot0_sha256": BOOT0_SHA256,
    }.items():
        require(report.get(key) == expected, f"Unexpected FEL DDR manifest: {key}")
    data = {}
    for key, address in (
        ("application", report["origin"]), ("loader", ENTRY), ("vendor", VENDOR_STAGE)
    ):
        item = report["artifacts"][key]
        content = (manifest.resolve().parent / artifact_name(item["file"])).read_bytes()
        require(item["address"] == address, f"Unexpected {key} address")
        require(
            len(content) == item["size"] and sha256(content) == item["sha256"],
            f"{key} differs from manifest",
        )
        data[key] = content
    check_application(data["application"], report["origin"], report["length"])
    check_helper(data["loader"])
    require(
        len(data["vendor"]) == VENDOR_SIZE and sha256(data["vendor"]) == VENDOR_SHA256,
        "Unrecognized extracted DRAM code",
    )
    return report, data


def check_version(xfel):
    version = invoke(xfel, "version")
    chip = re.search(r"AWUSBFEX\s+ID=0x([0-9a-fA-F]+)\b", version)
    scratch = re.search(r"scratchpad=0x([0-9a-fA-F]+)\b", version)
    require(
        chip is not None
        and int(chip.group(1), 16) == CHIP_ID
        and scratch is not None
        and int(scratch.group(1), 16) == SCRATCHPAD,
        "Expected R328 BROM FEL ID 0x00182100 and scratchpad 0x27e00",
    )


def run(manifest, xfel="xfel", dry_run=False):
    # Snapshot and validate all files before talking to USB.
    report, data = load_bundle(manifest)
    if dry_run:
        print(
            f"FEL DDR layout OK: entry 0x{report['origin']:08x}, "
            f"{len(data['application'])} bytes; no USB access"
        )
        return
    print("Enter BROM FEL, then immediately RELEASE the FEL button before downloading.", flush=True)
    check_version(xfel)
    with tempfile.TemporaryDirectory(prefix="r328-fel-ddr-") as directory:
        root = Path(directory)
        chunk, readback = root / "chunk.bin", root / "readback.bin"

        def read(address, size):
            readback.unlink(missing_ok=True)
            invoke(xfel, "read", hex(address), str(size), str(readback))
            result = readback.read_bytes()
            require(len(result) == size, "FEL read returned an incomplete buffer")
            return result

        def write_verified(address, content):
            chunk.write_bytes(content)
            invoke(xfel, "write", hex(address), str(chunk))
            require(
                read(address, len(content)) == content,
                "FEL readback mismatch; application NOT executed",
            )

        write_verified(VENDOR_STAGE, data["vendor"])
        write_verified(ENTRY, data["loader"])
        # Clear stale success before execution, even if a previous init returned.
        write_verified(MAILBOX, bytes(MESSAGE.size))
        try:
            invoke(xfel, "exec", hex(ENTRY))
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
            raise ValueError(
                "DRAM loader did not return to FEL; reset into BROM FEL before retrying"
            ) from error
        magic, abi, status, size_mib = MESSAGE.unpack(read(MAILBOX, MESSAGE.size))
        require(
            magic == MAGIC and abi == ABI,
            "Invalid DRAM loader result; application NOT executed",
        )
        require(status == 2, f"DRAM loader: {ERRORS.get(status, f'unknown status {status}')}")
        require(
            0 < size_mib <= DDR_SIZE // 1024**2
            and report["origin"] + report["length"] <= DDR_BASE + size_mib * 1024**2,
            f"DRAM loader reported {size_mib} MiB; application region does not fit",
        )
        check_version(xfel)
        print(
            f"DRAM initialized: {size_mib} MiB. BROM FEL returned; downloading application.",
            flush=True,
        )
        app = data["application"]
        for offset in range(0, len(app), CHUNK_SIZE):
            write_verified(report["origin"] + offset, app[offset : offset + CHUNK_SIZE])
        print(
            f"DDR readback PASS: {len(app)} bytes. Executing application (FEL will stop).",
            flush=True,
        )
        try:
            invoke(xfel, "exec", hex(report["origin"]))
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
            raise ValueError(
                "Application exec disconnected or timed out; it may be running. "
                "Check application output"
            ) from error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    pack = commands.add_parser("package")
    for name in ("binary", "elf", "helper", "helper-elf", "boot0", "vendor-output", "manifest"):
        pack.add_argument("--" + name, type=Path, required=True)
    pack.add_argument("--origin", type=lambda value: int(value, 0), default=DDR_BASE)
    pack.add_argument("--length", type=lambda value: int(value, 0), default=DDR_SIZE)
    load = commands.add_parser("run")
    load.add_argument("--manifest", type=Path, required=True)
    load.add_argument("--xfel", default="xfel")
    load.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if args.command == "package":
        package(
            args.binary, args.elf, args.helper, args.helper_elf, args.boot0,
            args.vendor_output, args.manifest, args.origin, args.length,
        )
    else:
        run(args.manifest, args.xfel, args.dry_run)


if __name__ == "__main__":
    try:
        main()
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
        print(error.stdout or "", file=sys.stderr)
        print(error.stderr or "", file=sys.stderr)
        sys.exit("fel_dram: USB transfer failed; application NOT executed. Reset into FEL.")
    except (ValueError, OSError, KeyError, TypeError, struct.error) as error:
        if isinstance(error.__cause__, (subprocess.CalledProcessError, subprocess.TimeoutExpired)):
            print(error.__cause__.stdout or "", file=sys.stderr)
            print(error.__cause__.stderr or "", file=sys.stderr)
        sys.exit(f"fel_dram: {error}")
