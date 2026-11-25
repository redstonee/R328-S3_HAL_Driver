"""Run the actual stackless ARM loader in QEMU; no R328 MMIO is emulated."""

import argparse
import struct
import subprocess
import tempfile
from pathlib import Path

# The probe compares every relocated byte with an independent reference in
# emulated RAM, then exits QEMU through semihosting. No stack is needed.
PROBE = r"""
.syntax unified
.cpu cortex-a7
.arm
.global _start
_start:
    ldr r0, =0x20000
    ldr r1, =0x50000
    ldr r2, =IMAGE_SIZE
1:
    ldrb r3, [r0], #1
    ldrb r4, [r1], #1
    cmp r3, r4
    bne 2f
    subs r2, r2, #1
    bne 1b
    ldr r1, =success
    b 3f
2:
    ldr r1, =failure
3:
    mov r0, #0x20
    svc 0x123456
    b .
success: .word 0x20026, 0
failure: .word 0x20026, 1
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--loader", type=Path, required=True)
    parser.add_argument("--cc", required=True)
    parser.add_argument("--objcopy", required=True)
    parser.add_argument("--qemu", required=True)
    args = parser.parse_args()
    template = args.loader.read_bytes()
    assert template[-16:] == struct.pack("<4sIII", b"R328", 0x28000, 0x20000, 0)
    with tempfile.TemporaryDirectory(prefix="r328-relocate-test-") as directory:
        root = Path(directory)
        source, elf, binary = root / "probe.S", root / "probe.elf", root / "probe.bin"
        loader = root / "loader.bin"
        # Include a >32 KiB copy that overlaps the source, and the exact upper
        # transport boundary at 0x38000, below the BROM high work area.
        for size in (256, 0xA000, 0x10000):
            source.write_text(PROBE.replace("IMAGE_SIZE", str(size)), encoding="ascii")
            subprocess.run(
                [
                    args.cc,
                    "-mcpu=cortex-a7",
                    "-marm",
                    "-nostdlib",
                    "-Wl,--build-id=none",
                    "-Wl,-Ttext=0x20000",
                    "-Wl,-e,_start",
                    str(source),
                    "-o",
                    str(elf),
                ],
                check=True,
                capture_output=True,
            )
            subprocess.run([args.objcopy, "-O", "binary", str(elf), str(binary)], check=True)
            code = binary.read_bytes()
            assert len(code) <= size
            binary.write_bytes(
                code + bytes((i * 37 + (i >> 8)) & 255 for i in range(size - len(code)))
            )
            loader.write_bytes(template[:-4] + struct.pack("<I", size))
            result = subprocess.run(
                [
                    args.qemu,
                    "-M",
                    "versatilepb",
                    "-cpu",
                    "cortex-a7",
                    "-m",
                    "16M",
                    "-display",
                    "none",
                    "-serial",
                    "none",
                    "-monitor",
                    "none",
                    "-nic",
                    "none",
                    "-audio",
                    "driver=none",
                    "-semihosting-config",
                    "enable=on,target=native",
                    "-device",
                    f"loader,file={binary},addr=0x28000,force-raw=on",
                    "-device",
                    f"loader,file={binary},addr=0x50000,force-raw=on",
                    "-device",
                    f"loader,file={loader},addr=0x27e00,force-raw=on,cpu-num=0",
                ],
                check=False,
                capture_output=True,
                text=True,
                timeout=20,
            )
            if result.returncode != 0:
                raise RuntimeError(
                    f"Relocation failed ({size} bytes): {result.stdout}{result.stderr}"
                )
            print(f"ARM relocation PASS: {size} bytes, 0x28000 -> 0x20000")


if __name__ == "__main__":
    main()
