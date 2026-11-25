.syntax unified
.cpu cortex-a7
.arm
#include "fel_nor.h"
.section .text.entry, "ax", %progbits
.global fel_nor_entry
.type fel_nor_entry, %function
fel_nor_entry:
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
    ldr sp, =FEL_NOR_STACK_TOP
    mov r0, #0
    mcr p15, 0, r0, c7, c5, 0
    mcr p15, 0, r0, c7, c5, 6
    dsb sy
    isb sy
    bl fel_nor_dispatch
    b .Lreturn
.Lcpu_error:
    ldr r0, =FEL_NOR_MAILBOX
    mov r1, #9                      /* FEL_NOR_CPU */
    str r1, [r0, #24]
.Lreturn:
    dsb sy
    mov sp, r5
    msr cpsr_cxsf, r4
    pop {r0-r12, lr}
    bx lr
.size fel_nor_entry, . - fel_nor_entry

.section .fel_nor_abi, "a", %progbits
.balign 4
    .ascii "R328NOR1"
    .word FEL_NOR_ENTRY, FEL_NOR_MAILBOX, FEL_NOR_BUFFER, FEL_NOR_BUFFER_SIZE
    .word FEL_NOR_STACK_BOTTOM, FEL_NOR_STACK_TOP, FEL_NOR_MAGIC, 1
.section .note.GNU-stack, "", %progbits
