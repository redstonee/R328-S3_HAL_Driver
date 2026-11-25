/* Replace only the unused SPI NAND reader. Preserve the TOC parser at 0x260dc. */
ENTRY(nor_boot_load)
MEMORY { PATCH (rx) : ORIGIN = 0x25564, LENGTH = 0xb5c }
PHDRS { reader PT_LOAD FLAGS(5); }
SECTIONS
{
    .text :
    {
        KEEP(*(.text.entry))
        *(.text*)
        *(.rodata*)
    } > PATCH :reader
    .data : { *(.data*) *(.bss*) *(COMMON) } > PATCH :reader
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) }
}
ASSERT(SIZEOF(.data) == 0, "NOR reader must not require data/BSS initialization")
