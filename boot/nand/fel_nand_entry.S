.syntax unified
.cpu cortex-a7
.arm
#include "fel_nand.h"
.section .text.entry, "ax", %progbits
.global fel_nand_entry
.type fel_nand_entry, %function
fel_nand_entry:
    /* BROM calls us as a function. Preserve its mode, flags, interrupt masks,
     * registers and stack; never run the application's nonreturning startup.
     */
    push {r0-r12, lr}
    mrs r4, cpsr
    mov r5, sp
    cpsid if
    mrc p15, 0, r0, c1, c0, 0
    tst r0, #5                      /* Require MMU and D-cache off. */
    bne .Lcpu_error
    mrc p15, 0, r0, c0, c0, 5
    tst r0, #3
    bne .Lcpu_error
    ldr sp, =FEL_NAND_STACK_TOP
    mov r0, #0
    mcr p15, 0, r0, c7, c5, 0
    mcr p15, 0, r0, c7, c5, 6
    dsb sy
    isb sy
    bl fel_nand_dispatch
    b .Lreturn
.Lcpu_error:
    ldr r0, =FEL_NAND_MAILBOX
    mov r1, #9                      /* FEL_NAND_CPU */
    str r1, [r0, #24]
.Lreturn:
    dsb sy
    mov sp, r5
    msr cpsr_cxsf, r4
    pop {r0-r12, lr}
    bx lr
.size fel_nand_entry, . - fel_nand_entry

.section .fel_nand_abi, "a", %progbits
.balign 4
    .ascii "R328NAN1"
    .word FEL_NAND_ENTRY, FEL_NAND_MAILBOX, FEL_NAND_BUFFER, FEL_NAND_BUFFER_SIZE
    .word FEL_NAND_STACK_BOTTOM, FEL_NAND_STACK_TOP, FEL_NAND_MAGIC, 1
.section .note.GNU-stack, "", %progbits
