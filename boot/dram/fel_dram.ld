OUTPUT_ARCH(arm)
ENTRY(fel_dram_entry)
MEMORY { HELPER (rwx) : ORIGIN = 0x2c000, LENGTH = 0xf00 }
SECTIONS
{
    .text : { KEEP(*(.text.entry)) *(.text*) KEEP(*(.fel_dram_abi)) } > HELPER
    /DISCARD/ : { *(.comment) *(.note*) *(.ARM.attributes) }
}
