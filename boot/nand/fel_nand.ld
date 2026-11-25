ENTRY(fel_nand_entry)
MEMORY { CODE (rx) : ORIGIN = 0x28000, LENGTH = 0x1000 }
PHDRS { helper PT_LOAD FLAGS(5); }
SECTIONS
{
    .text :
    {
        KEEP(*(.text.entry))
        *(.text*)
        *(.rodata .rodata.*)
    } > CODE :helper
    .abi : ALIGN(4) { KEEP(*(.fel_nand_abi)) } > CODE :helper
    .data : { *(.data*) *(.bss*) *(COMMON) } > CODE :helper
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) }
}
ASSERT(SIZEOF(.data) == 0, "FEL NAND helper must not require data/BSS initialization")
