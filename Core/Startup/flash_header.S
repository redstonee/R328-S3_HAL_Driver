.syntax unified
.cpu cortex-a7
.arm

/* The vendor boot0 reads the destination from payload + 0x2c.  Its
 * "freertos" TOC1 dispatch copies this payload and branches to it without
 * modifying a U-Boot header or starting a secure monitor.  The name is an
 * on-disk dispatch key; the application has no FreeRTOS dependency.
 *
 * The eight real exception vectors occupy bytes 0..31.  Keep metadata and
 * padding between those vectors and Reset_Handler, all in the ELF/BIN.
 */
.section .boot_header, "a", %progbits
.balign 4
    .word 0
    .word 0
    .word 0
    .word 0x40000000                 /* Absolute payload offset 0x2c. */
    .space 512 - 48

.section .note.GNU-stack, "", %progbits
