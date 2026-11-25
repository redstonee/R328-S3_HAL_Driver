#!/usr/bin/env python3
"""Package and load the R328 raw SRAM image using xfel (no Flash/DRAM commands)."""

import argparse
import hashlib
import json
import re
import shlex
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ORIGIN = 0x28000
LENGTH = 0x8000
STACKS = 8192 + 2048 + 3 * 512
CHIP_ID = 0x00182100
FULL_ORIGIN = 0x20000
FULL_LENGTH = 0x20000
LOADER_ADDRESS = 0x27E00
RELOCATOR_ADDRESS = 0x3FF00
# Leave the top 32 KiB untouched until BROM has relinquished control.
STAGING_LIMIT = 0x10000
LOADER_FOOTER = struct.Struct("<4sIII")


def profile_layout(profile):
    require(profile in ("DIRECT", "FULL"), "Unknown FEL SRAM profile")
    return (FULL_ORIGIN, FULL_LENGTH) if profile == "FULL" else (ORIGIN, LENGTH)


def check_loader(data, image_size):
    require(32 <= len(data) <= 0x100 and len(data) % 4 == 0, "Invalid FEL loader size")
    require(
        LOADER_FOOTER.unpack(data[-LOADER_FOOTER.size :])
        == (b"R328", ORIGIN, FULL_ORIGIN, image_size),
        "Invalid FEL loader parameters",
    )


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def check_binary(data, profile="DIRECT"):
    _, length = profile_layout(profile)
    limit = STAGING_LIMIT if profile == "FULL" else length - STACKS
    require(32 <= len(data) <= limit, f"FEL image exceeds {profile} BIN limit ({limit} bytes)")
    for index, (word,) in enumerate(struct.iter_unpack("<I", data[:32])):
        require(word >> 24 == 0xEA, "Expected raw ARM branch vectors (no eGON/uImage header)")
        offset = word & 0xFFFFFF
        if offset & 0x800000:
            offset -= 1 << 24
        require(0 <= index * 4 + 8 + offset * 4 < len(data), "Vector target outside BIN")


def package(
    binary, elf, output, manifest, uart0_route, profile="DIRECT", loader=None, loader_output=None
):
    origin, length = profile_layout(profile)
    inputs = {binary.resolve(), elf.resolve()}
    outputs = [output, manifest]
    loader_data = None
    data, header = binary.read_bytes(), elf.read_bytes()
    check_binary(data, profile)
    require(
        len(header) >= 52
        and header[:7] == b"\x7fELF\x01\x01\x01"
        and struct.unpack_from("<H", header, 18)[0] == 40
        and struct.unpack_from("<I", header, 24)[0] == origin,
        f"Expected ARM ELF entry 0x{origin:x}",
    )
    if profile == "FULL":
        require(loader is not None and loader_output is not None, "FULL profile requires a loader")
        inputs.add(loader.resolve())
        outputs.append(loader_output)
        template = loader.read_bytes()
        check_loader(template, 0)
        loader_data = template[:-4] + struct.pack("<I", len(data))
    else:
        require(loader is None and loader_output is None, "DIRECT profile does not use a loader")
    resolved = {path.resolve() for path in outputs}
    require(
        len(resolved) == len(outputs) and not inputs & resolved, "Outputs must differ from inputs"
    )
    require(
        len({path.parent.resolve() for path in outputs}) == 1,
        "Keep BIN, loader and manifest together",
    )
    for path in outputs:
        path.parent.mkdir(parents=True, exist_ok=True)
    report = {
        "format": "r328-fel-relocate-v1" if profile == "FULL" else "r328-fel-raw-v1",
        "binary": output.name,
        "load_address": origin,
        "entry": origin,
        "ram_length": length,
        "chip_id": CHIP_ID,
        "size": len(data),
        "sha256": digest(data),
        "uart0_route": uart0_route,
        "baud": 115200,
    }
    if loader_data is not None:
        report.update(
            {
                "staging_address": ORIGIN,
                "loader_address": LOADER_ADDRESS,
                "relocator_address": RELOCATOR_ADDRESS,
                "loader_binary": loader_output.name,
                "loader_size": len(loader_data),
                "loader_sha256": digest(loader_data),
            }
        )
        loader_output.write_bytes(loader_data)
    output.write_bytes(data)
    manifest.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"FEL {profile} image: {output}, {len(data)} bytes, runtime entry=0x{origin:08x}")


def read_artifact(path, name, size, sha256):
    require(
        isinstance(name, str) and bool(re.fullmatch(r"[A-Za-z0-9_-]+\.bin", name)),
        "Invalid image filename",
    )
    binary = path.resolve().parent / name
    data = binary.read_bytes()
    require(
        len(data) == size and digest(data) == sha256,
        "FEL BIN differs from manifest; rebuild before loading",
    )
    return binary, data


def load_bundle(path):
    report = json.loads(path.read_text(encoding="utf-8"))
    formats = {"r328-fel-raw-v1": "DIRECT", "r328-fel-relocate-v1": "FULL"}
    profile = formats.get(report["format"])
    origin, length = profile_layout(profile)
    require(
        report["load_address"] == origin
        and report["entry"] == origin
        and report["ram_length"] == length
        and report["chip_id"] == CHIP_ID,
        "Unexpected FEL image profile",
    )
    binary, data = read_artifact(path, report["binary"], report["size"], report["sha256"])
    check_binary(data, profile)
    artifacts = [(ORIGIN, binary.name, data)]
    if profile == "FULL":
        require(
            report["staging_address"] == ORIGIN
            and report["loader_address"] == LOADER_ADDRESS
            and report["relocator_address"] == RELOCATOR_ADDRESS,
            "Unexpected FEL staging layout",
        )
        loader, loader_data = read_artifact(
            path, report["loader_binary"], report["loader_size"], report["loader_sha256"]
        )
        require(loader != binary, "Loader must differ from application")
        check_loader(loader_data, len(data))
        artifacts.append((LOADER_ADDRESS, loader.name, loader_data))
    return profile, artifacts


def load_manifest(path):
    # Preserve the original single-image API for DIRECT callers.
    _, artifacts = load_bundle(path)
    _, name, data = artifacts[0]
    return path.resolve().parent / name, data


def invoke(xfel, *args):
    command = [xfel, *args]
    print("+ " + shlex.join(command), flush=True)
    result = subprocess.run(command, check=True, text=True, capture_output=True, timeout=20)
    if result.stdout:
        print(result.stdout, end="" if result.stdout.endswith("\n") else "\n")
    return result.stdout


def failure_message(error):
    command = error.cmd
    operation = command[1] if isinstance(command, (list, tuple)) and len(command) > 1 else "unknown"
    if operation == "exec":
        return "FEL exec failed or timed out; execution may have started: check UART"
    if operation in ("write", "read"):
        return f"FEL {operation} failed; application was NOT executed. Re-enter BROM FEL before retrying"
    return f"FEL {operation} failed; application was NOT downloaded or executed. Check USB and reset into FEL"


def run(manifest, xfel="xfel", dry_run=False):
    profile, artifacts = load_bundle(manifest)
    entry = LOADER_ADDRESS if profile == "FULL" else ORIGIN
    if dry_run:
        origin, length = profile_layout(profile)
        print(
            f"FEL {profile} layout OK: runtime 0x{origin:08x}, {length // 1024} KiB; "
            f"{len(artifacts)} verified download(s), exec 0x{entry:08x} (no USB access)"
        )
        return
    version = invoke(xfel, "version")
    match = re.search(r"AWUSBFEX\s+ID=0x([0-9a-fA-F]+)\b", version)
    require(
        match is not None and int(match.group(1), 16) == CHIP_ID,
        "Expected R328 BROM FEL ID 0x00182100; nothing downloaded",
    )
    scratch = re.search(r"scratchpad=0x([0-9a-fA-F]+)\b", version)
    require(scratch is not None, "Missing BROM scratchpad address; nothing downloaded")
    scratch_address = int(scratch.group(1), 16)
    require(
        scratch_address + 0x200 <= ORIGIN,
        "BROM scratchpad overlaps the staging SRAM region; nothing downloaded",
    )
    if profile == "FULL":
        require(
            scratch_address == LOADER_ADDRESS,
            "Expected R328 scratchpad at 0x27e00; nothing downloaded",
        )
    # Snapshot every validated artifact before USB access; never copy the
    # runtime image over BROM while the FEL protocol is still serving USB.
    with tempfile.TemporaryDirectory(prefix="r328-fel-") as directory:
        for index, (address, name, data) in enumerate(artifacts):
            snapshot = Path(directory) / name
            snapshot.write_bytes(data)
            readback = Path(directory) / f"readback-{index}.bin"
            invoke(xfel, "write", hex(address), str(snapshot))
            invoke(xfel, "read", hex(address), str(len(data)), str(readback))
            require(readback.read_bytes() == data, "SRAM readback mismatch; execution cancelled")
    print("SRAM readback verified. Executing; observe UART0 (115200 8N1).", flush=True)
    invoke(xfel, "exec", hex(entry))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    pack = commands.add_parser("package")
    for name in ("binary", "elf", "output", "manifest"):
        pack.add_argument("--" + name, type=Path, required=True)
    pack.add_argument("--uart0-route", choices=("PH0_PH1", "PB8_PB9"), required=True)
    pack.add_argument("--profile", choices=("DIRECT", "FULL"), default="DIRECT")
    pack.add_argument("--loader", type=Path)
    pack.add_argument("--loader-output", type=Path)
    load = commands.add_parser("run")
    load.add_argument("--manifest", type=Path, required=True)
    load.add_argument("--xfel", default="xfel")
    load.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if args.command == "package":
        package(
            args.binary,
            args.elf,
            args.output,
            args.manifest,
            args.uart0_route,
            args.profile,
            args.loader,
            args.loader_output,
        )
    else:
        run(args.manifest, args.xfel, args.dry_run)


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        print(error.stdout or "", file=sys.stderr)
        print(error.stderr or "", file=sys.stderr)
        sys.exit(failure_message(error))
    except subprocess.TimeoutExpired as error:
        sys.exit(failure_message(error))
    except (ValueError, OSError, KeyError, TypeError, struct.error) as error:
        sys.exit(f"fel_load: {error}")
