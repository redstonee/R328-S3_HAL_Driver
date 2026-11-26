"""Check the actual linked ARM artifact; requires no third-party Python packages."""

import argparse
from pathlib import Path
import struct
import subprocess


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    parser = argparse.ArgumentParser()
    for name in ("elf", "binary", "nm", "objdump"):
        parser.add_argument(f"--{name}", required=True)
    parser.add_argument("--origin", type=lambda s: int(s, 0), required=True)
    parser.add_argument("--length", type=lambda s: int(s, 0), required=True)
    args = parser.parse_args()
    elf = Path(args.elf).read_bytes()
    binary = Path(args.binary).read_bytes()
    check(elf[:7] == b"\x7fELF\x01\x01\x01", "Expected ELF32 little-endian")
    check(struct.unpack_from("<H", elf, 18)[0] == 40, "Expected ARM machine")
    check(struct.unpack_from("<I", elf, 24)[0] == args.origin, "Entry must equal load origin")
    symbols = {}
    output = subprocess.check_output([args.nm, "-n", args.elf], text=True)
    for line in output.splitlines():
        fields = line.split()
        if len(fields) == 3:
            symbols[fields[2]] = int(fields[0], 16)
    for name in ("_start", "Reset_Handler", "IRQ_Entry", "IRQ_Handler", "Fault_Handler", "main"):
        check(name in symbols, f"Missing startup symbol {name}")
    check(symbols["_start"] == args.origin, "Vectors must start at image origin")
    for index in range(8):
        word = struct.unpack_from("<I", binary, index * 4)[0]
        check(word >> 24 == 0xEA, f"Vector {index} is not an ARM branch")
        displacement = word & 0xFFFFFF
        if displacement & 0x800000:
            displacement -= 1 << 24
        target = args.origin + index * 4 + 8 + displacement * 4
        check(args.origin <= target < symbols["__image_end__"], "Vector target outside image")
    check(len(binary) == symbols["__image_end__"] - args.origin, "Binary image length mismatch")
    check(symbols["__data_load__"] == symbols["__data_start__"], "RAM data must load in place")
    check(symbols["__static_end__"] <= symbols["__stack_bottom__"], "Static data overlaps stacks")
    check(
        symbols["__static_end__"] <= symbols["__heap_start__"] <= symbols["__heap_end__"],
        "Invalid heap interval",
    )
    check(symbols["__heap_start__"] % 8 == 0, "Unaligned heap start")
    check(symbols["__heap_end__"] == symbols["__stack_bottom__"], "Heap overlaps reserved stacks")
    check(symbols["__svc_stack_top__"] == args.origin + args.length, "Wrong RAM boundary")
    for mode in ("svc", "irq", "fiq", "abt", "und"):
        check(symbols[f"__{mode}_stack_top__"] % 8 == 0, f"Unaligned {mode} stack")
    undefined = subprocess.check_output([args.nm, "-u", args.elf], text=True).strip()
    check(not undefined, f"Unresolved symbols: {undefined}")
    disasm = subprocess.check_output([args.objdump, "-d", args.elf], text=True)
    check("spsr" in disasm.lower() and "{r0, r1, r2" in disasm, "Missing IRQ context preservation")
    check("<HAL_Init>" in disasm, "Missing HAL initialization")
    print(
        f"ARM layout OK: origin={args.origin:#010x}, image={len(binary)} bytes, "
        f"free before stacks={symbols['__stack_bottom__'] - symbols['__static_end__']} bytes"
    )


if __name__ == "__main__":
    main()
