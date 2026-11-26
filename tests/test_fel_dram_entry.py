"""Run the real returning DRAM wrapper in QEMU; replace only vendor/MMIO code."""

import argparse
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VENDOR = r"""
.syntax unified
.cpu cortex-a7
.thumb
.section .vendor, "ax"
.global vendor
.thumb_func
vendor:
    cmp r0, #0
    bne bad
    ldr r2, =0x20038
    cmp r1, r2
    bne bad
    ldr r0, =0x27b44
    ldr r1, =0x27dec
1:
    ldr r2, [r0], #4
    cmp r2, #0
    bne bad
    cmp r0, r1
    blo 1b
    ldr r0, =0x38000
    cmp sp, r0
    bne bad
    sub sp, sp, #2048
    movs r0, #0x55
    str r0, [sp]
    add sp, sp, #2048
    /* Simulate parameter updates, BSS and scratchpad writes. */
    ldr r0, =0x20000
    str r2, [r0]
    str r2, [r0, #0x38]
    ldr r0, =0x27b44
    str r2, [r0]
    ldr r0, =0x27e00
    str r2, [r0]
    ldr r0, =0x60000
    ldr r1, [r0, #4]
    cbz r1, 2f
    ldr r2, =0x35000
    str r1, [r2]
2:
    ldr r0, [r0]
    bx lr
bad:
    ldr r0, =0x60008
    movs r1, #1
    str r1, [r0]
    movs r0, #0
    bx lr
"""
SOURCE = r"""
.syntax unified
.cpu cortex-a7
.arm
.section .test, "ax"
.global _start
_start:
    mrs r0, cpsr
    and r0, r0, #0x1f
    cmp r0, #0x1a
    bne svc_entry
    adr r0, svc_entry
    msr elr_hyp, r0
    mov r0, #0xd3
    msr spsr_cxsf, r0
    eret
svc_entry:
    /* Known complete BROM low SRAM pattern; later verify every word. */
    ldr r0, =0x20000
    ldr r1, =0x28000
    ldr r2, =0x19385abc
1:
    eor r3, r0, r2
    str r3, [r0], #4
    cmp r0, r1
    blo 1b
    ldr r0, =0x28000
    ldr r1, =0x2b100
2:
    mov r2, #0x77
    str r2, [r0], #4
    cmp r0, r1
    blo 2b
    ldr r0, =vendor_bytes
    ldr r1, =0x2adac
    ldr r2, =vendor_end
3:
    ldrb r3, [r0], #1
    strb r3, [r1], #1
    cmp r0, r2
    blo 3b
    ldr r0, =0x38000
    ldr r1, =0x9abcdef0
    str r1, [r0]

.macro check_call flags, original_sp, capacity, expected_status, overflow=0
    ldr r0, =0x60000
    mov r1, #\capacity
    str r1, [r0]
    mov r1, #\overflow
    str r1, [r0, #4]
    mov r1, #0
    str r1, [r0, #8]
    ldr sp, =\original_sp
    ldr r0, =\flags
    msr cpsr_cxsf, r0
    mov r0, #16
    mov r1, #17
    mov r2, #18
    mov r3, #19
    mov r4, #20
    mov r5, #21
    mov r6, #22
    mov r7, #23
    mov r8, #24
    mov r9, #25
    mov r10, #26
    mov r11, #27
    mov r12, #28
    bl fel_dram_entry
return_\@:
    push {r0-r12, lr}
    mrs r2, cpsr
    ldr r3, =\flags
    cmp r2, r3
    bne failure
    ldr r0, =(\original_sp - 56)
    cmp sp, r0
    bne failure
    mov r1, #16
compare_\@:
    ldr r2, [r0], #4
    cmp r2, r1
    bne failure
    add r1, r1, #1
    cmp r1, #29
    bne compare_\@
    ldr r2, [r0]
    ldr r3, =return_\@
    cmp r2, r3
    bne failure
    add sp, sp, #56
    ldr r0, =0x2cf00
    ldr r1, [r0]
    ldr r2, =0x52444446
    cmp r1, r2
    bne failure
    ldr r1, [r0, #4]
    cmp r1, #1
    bne failure
    ldr r1, [r0, #8]
    cmp r1, #\expected_status
    bne failure
    ldr r0, =0x60008
    ldr r0, [r0]
    cmp r0, #0
    bne failure
    bl check_low
.endm
    check_call 0xa00000d3, 0x3c600, 128, 2
    check_call 0x60000013, 0x3c5dc, 0, 3
    check_call 0xa00000d3, 0x3c5dc, 128, 5, 1
    check_call 0x600000d3, 0x37000, 128, 5
    mrc p15, 0, r0, c1, c0, 0
    orr r0, r0, #4
    mcr p15, 0, r0, c1, c0, 0
    isb
    check_call 0x600000d3, 0x3c600, 128, 4
    mrc p15, 0, r0, c1, c0, 0
    bic r0, r0, #4
    mcr p15, 0, r0, c1, c0, 0
    isb
    ldr r1, =success
    b exit
check_low:
    ldr r0, =0x20000
    ldr r1, =0x28000
    ldr r2, =0x19385abc
4:
    eor r3, r0, r2
    ldr r12, [r0], #4
    cmp r3, r12
    bne failure
    cmp r0, r1
    blo 4b
    ldr r0, =0x38000
    ldr r0, [r0]
    ldr r1, =0x9abcdef0
    cmp r0, r1
    bne failure
    bx lr
failure:
    ldr r1, =failed
exit:
    mov r0, #0x20
    svc 0x123456
    b .
success: .word 0x20026, 0
failed: .word 0x20026, 1
.ltorg
.balign 4
vendor_bytes:
    .incbin "VENDOR_FILE"
vendor_end:
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("cc", "objcopy", "qemu"):
        parser.add_argument("--" + name, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="r328-fel-ddr-entry-") as directory:
        root = Path(directory)

        def build(name, source, script, extra=()):
            src, ld, elf, binary = [
                root / f"{name}.{suffix}" for suffix in ("S", "ld", "elf", "bin")
            ]
            src.write_text(source)
            ld.write_text(script)
            subprocess.run(
                [
                    args.cc,
                    "-mcpu=cortex-a7",
                    "-marm",
                    "-nostdlib",
                    "-Wl,--build-id=none",
                    f"-Wl,-T{ld}",
                    str(src),
                    *map(str, extra),
                    "-o",
                    str(elf),
                ],
                check=True,
                capture_output=True,
            )
            subprocess.run([args.objcopy, "-O", "binary", str(elf), str(binary)], check=True)
            return binary

        vendor = build("vendor", VENDOR, "SECTIONS { .vendor 0x22dac : { *(.vendor) } }")
        binary = build(
            "probe",
            SOURCE.replace("VENDOR_FILE", str(vendor)),
            "SECTIONS { .helper 0x2c000 : { *(.text.entry) *(.fel_dram_abi) } "
            ".test 0x50000 : { *(.test) } /DISCARD/ : { *(.note*) *(.ARM.attributes) } }",
            [ROOT / "boot/dram/fel_dram.S"],
        )
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
                f"loader,file={binary},addr=0x2c000,force-raw=on",
                "-device",
                "loader,addr=0x50000,cpu-num=0",
            ],
            check=False,
            capture_output=True,
            text=True,
            timeout=20,
        )
        if result.returncode != 0:
            raise RuntimeError(f"FEL DDR entry failed: {result.stdout}{result.stderr}")
        print(
            "ARM FEL DDR PASS: full BROM SRAM restore, private stack, BSS, "
            "registers/CPSR, error paths"
        )


if __name__ == "__main__":
    main()
