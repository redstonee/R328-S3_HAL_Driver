"""Execute the prebuilt's actual TOC copy and direct handoff in QEMU.

DDR training and NAND MMIO are NOT emulated: start immediately after boot0's
successful NAND read, and stub only UART printf and the MMIO-backed delay.
The checksum, parser, memcpy, dispatch and MMU/cache setup/shutdown execute
from the prebuilt. With --nor-boot0, also execute the patched dispatcher;
replace its reader body with a success return because TOC is preloaded.
"""

import argparse
import importlib.util
import json
import struct
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("nand_image", ROOT / "tools/nand_image.py")
tool = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(tool)

SETUP = r"""
.syntax unified
.cpu cortex-a7
.arm
.global _start
_start:
    cpsid if
    cps #0x13
    ldr sp, =0x47fff000
    sub sp, sp, #32
    mov r0, sp
    mov r1, #0
    mov r2, #8
1:
    str r1, [r0], #4
    subs r2, r2, #1
    bne 1b
    mov r0, #128
    ldr r3, =0x20c27
    blx r3                       /* Actual boot0 MMU setup; delay is stubbed. */
    ldr r0, =0x42e00000
    ldr r1, [r0, #36]
    ldr r3, =0x21301
    blx r3                       /* Actual boot0 TOC1 checksum implementation. */
    cmp r0, #0
    bne checksum_failure
    mov r5, #128
    mov r6, #0
    ldr r0, =0x26405
    bx r0
checksum_failure:
    ldr r1, =failed
    mov r0, #0x20
    svc 0x123456
    b .
failed: .word 0x20026, 1
"""

PROBE = r"""
.syntax unified
.cpu cortex-a7
.arm
.global _start
_start:
.rept 8
    b probe
.endr
.org 0x2c
    .word 0x40000000
.org 512
probe:
    mrs r0, cpsr
    and r0, r0, #0x1f
    cmp r0, #0x13
    bne failure
    mrc p15, 0, r0, c1, c0, 0
    tst r0, #5
    bne failure
    ldr r0, =0x40000000
    ldr r1, =0x42e00200
    ldr r2, =IMAGE_SIZE
1:
    ldrb r3, [r0], #1
    ldrb r4, [r1], #1
    cmp r3, r4
    bne failure
    subs r2, r2, #1
    bne 1b
    ldr r1, =success
    b exit
failure:
    ldr r1, =failed
exit:
    mov r0, #0x20
    svc 0x123456
    b .
success: .word 0x20026, 0
failed: .word 0x20026, 1
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", required=True)
    parser.add_argument("--objcopy", required=True)
    parser.add_argument("--qemu", required=True)
    parser.add_argument("--nor-boot0", type=Path)
    args = parser.parse_args()
    profile = json.loads((ROOT / "prebuilt/r328/spinand.json").read_text())
    prebuilt = (ROOT / "prebuilt/r328/boot0-spinand.bin").read_bytes()
    boot0 = bytearray(tool.configure_boot0(prebuilt, profile, "PH0_PH1"))
    setup_source = SETUP
    if args.nor_boot0:
        boot0 = bytearray(args.nor_boot0.read_bytes())
        # NOR PIO is covered by native register/FIFO tests. QEMU has no R328
        # SPI controller: preload TOC and stub only the reader body here. The
        # actual patched dispatcher, Thumb return and vendor handoff execute.
        boot0[0x5564:0x5568] = bytes.fromhex("00207047")  # movs r0,#0; bx lr
        setup_source = SETUP.replace(
            "    mov r5, #128",
            "    ldr r3, =0x260c1\n    blx r3\n    cmp r0, #0\n"
            "    bne checksum_failure\n    mov r5, #128",
        )
    # These stubs apply ONLY to the emulation fixture, never to an output binary.
    for offset in (0x12AC, 0xBF8):
        boot0[offset : offset + 2] = struct.pack("<H", 0x4770)  # bx lr (Thumb)
    with tempfile.TemporaryDirectory(prefix="r328-nand-boot-test-") as directory:
        root = Path(directory)

        def assemble(name, source, origin):
            asm, elf, binary = (root / (name + suffix) for suffix in (".S", ".elf", ".bin"))
            asm.write_text(source)
            subprocess.run(
                [
                    args.cc,
                    "-mcpu=cortex-a7",
                    "-marm",
                    "-nostdlib",
                    "-Wl,--build-id=none",
                    f"-Wl,-Ttext={origin}",
                    "-Wl,-e,_start",
                    str(asm),
                    "-o",
                    str(elf),
                ],
                check=True,
                capture_output=True,
            )
            subprocess.run([args.objcopy, "-O", "binary", str(elf), str(binary)], check=True)
            return binary

        setup = assemble("setup", setup_source, "0x30000")
        loader = root / "boot0.bin"
        flash = bytearray(0x40000)
        flash[0x20000:0x28000] = boot0
        flash[0x30000 : 0x30000 + len(setup.read_bytes())] = setup.read_bytes()
        loader.write_bytes(flash)
        for size in (1024, 150000):
            application = assemble("probe", PROBE.replace("IMAGE_SIZE", str(size)), "0x40000000")
            code = application.read_bytes()
            assert len(code) < size
            firmware = code + bytes((i * 37 + (i >> 8)) & 255 for i in range(size - len(code)))
            package = root / "package.bin"
            package.write_bytes(tool.make_package(firmware))
            result = subprocess.run(
                [
                    args.qemu,
                    "-M",
                    "virt,secure=on",
                    "-cpu",
                    "cortex-a7",
                    "-m",
                    "128M",
                    "-display",
                    "none",
                    "-serial",
                    "none",
                    "-monitor",
                    "none",
                    "-nic",
                    "none",
                    "-semihosting-config",
                    "enable=on,target=native",
                    "-bios",
                    str(loader),
                    "-device",
                    f"loader,file={package},addr=0x42e00000,force-raw=on",
                    "-device",
                    "loader,addr=0x30000,cpu-num=0",
                ],
                check=False,
                capture_output=True,
                text=True,
                timeout=20,
            )
            if result.returncode != 0:
                raise RuntimeError(
                    f"Direct boot failed ({size} bytes): {result.stdout}{result.stderr}"
                )
            print(
                f"boot0 direct copy/handoff PASS: {size} bytes -> 0x40000000, SVC, MMU/D-cache off"
            )


if __name__ == "__main__":
    main()
