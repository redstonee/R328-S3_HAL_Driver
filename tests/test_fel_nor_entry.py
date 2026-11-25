"""Execute the actual ARM return-to-BROM entry in QEMU, with a C ABI stub."""

import argparse
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r"""
.syntax unified
.cpu cortex-a7
.arm
.section .text.stub, "ax"
.thumb
.global fel_nor_dispatch
.thumb_func
fel_nor_dispatch:
    ldr r0, =0x30000
    cmp sp, r0
    bne failure
    sub sp, sp, #512
    mov r0, #0x55
    str r0, [sp]
    add sp, sp, #512
    mov r0, #0xff
    mov r1, #0xff
    mov r2, #0xff
    mov r3, #0xff
    mov r12, #0xff
    cmp r0, #0
    bx lr
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
.macro check_call flags, original_sp
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
    bl fel_nor_entry
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
.endm
    check_call 0xa00000d3, 0x3c600
    check_call 0x60000013, 0x3c5dc
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
    parser.add_argument("--medium", choices=("nor", "nand"), default="nor")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="r328-fel-nor-entry-") as directory:
        root = Path(directory)
        source, script, elf, binary = [
            root / name for name in ("probe.S", "probe.ld", "probe.elf", "probe.bin")
        ]
        source.write_text(SOURCE.replace("fel_nor", f"fel_{args.medium}"))
        script.write_text(
            "SECTIONS { .helper 0x28000 : { *(.text.entry) *(.text*) } .test 0x50000 : { *(.test) } /DISCARD/ : { *(.note*) *(.ARM.attributes) } }"
        )
        subprocess.run(
            [
                args.cc,
                "-mcpu=cortex-a7",
                "-marm",
                "-nostdlib",
                "-Wl,--build-id=none",
                f"-Wl,-T{script}",
                str(ROOT / f"boot/{args.medium}/fel_{args.medium}_entry.S"),
                str(source),
                "-o",
                str(elf),
            ],
            check=True,
            capture_output=True,
        )
        subprocess.run([args.objcopy, "-O", "binary", str(elf), str(binary)], check=True)
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
                "loader,addr=0x50000,cpu-num=0",
            ],
            check=False,
            capture_output=True,
            text=True,
            timeout=20,
        )
        if result.returncode != 0:
            raise RuntimeError(f"FEL NOR entry failed: {result.stdout}{result.stderr}")
        print(
            "ARM return-to-BROM entry PASS: private stack, r0-r12/LR/SP/CPSR restored, repeated calls"
        )


if __name__ == "__main__":
    main()
