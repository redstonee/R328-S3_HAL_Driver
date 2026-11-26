.syntax unified
.cpu cortex-a7
.arm
#include "fel_dram.h"
.section .text.entry, "ax", %progbits
.global fel_dram_entry
.type fel_dram_entry, %function
fel_dram_entry:
    push {r0-r12, lr}
    mrs r4, cpsr
    mov r5, sp
    cpsid if
    ldr r0, =FEL_DRAM_MAILBOX
    ldr r1, =FEL_DRAM_MAGIC
    str r1, [r0]
    mov r1, #FEL_DRAM_ABI
    str r1, [r0, #4]
    mov r1, #0
    str r1, [r0, #12]
    mov r1, #FEL_DRAM_CPU_ERROR
    str r1, [r0, #8]
    mrc p15, 0, r1, c1, c0, 0
    tst r1, #5                      /* CPU0, MMU and D-cache disabled. */
    bne .Lreturn
    mrc p15, 0, r1, c0, c0, 5
    tst r1, #3
    bne .Lreturn
    mov r1, #FEL_DRAM_STACK_ERROR
    str r1, [r0, #8]
    ldr r1, =FEL_DRAM_BROM_STACK_MIN
    cmp sp, r1                     /* Never overwrite the active BROM stack. */
    blo .Lreturn
    ldr r1, =0x40000
    cmp sp, r1
    bhs .Lreturn
    mov r1, #FEL_DRAM_RUNNING
    str r1, [r0, #8]
    ldr sp, =FEL_DRAM_STACK_TOP
    ldr r0, =FEL_DRAM_STACK_BOTTOM
    ldr r1, =FEL_DRAM_STACK_CANARY
    str r1, [r0]

    /* Save the complete low 32 KiB, including BROM's scratchpad and state. */
    ldr r0, =FEL_DRAM_VENDOR_BASE
    ldr r1, =FEL_DRAM_BACKUP
    ldr r2, =(FEL_DRAM_VENDOR_BASE + FEL_DRAM_BACKUP_SIZE)
1:
    ldr r3, [r0], #4
    str r3, [r1], #4
    cmp r0, r2
    blo 1b
    ldr r0, =FEL_DRAM_VENDOR_STAGE
    ldr r1, =FEL_DRAM_VENDOR_BASE
    ldr r2, =(FEL_DRAM_VENDOR_STAGE + FEL_DRAM_VENDOR_SIZE)
2:
    ldr r3, [r0], #4
    str r3, [r1], #4
    cmp r0, r2
    blo 2b
    ldr r0, =FEL_DRAM_VENDOR_BSS
    ldr r1, =FEL_DRAM_VENDOR_BSS_END
    mov r2, #0
3:
    str r2, [r0], #4
    cmp r0, r1
    blo 3b
    bl .Lsync_code
    mov r0, #0
    ldr r1, =FEL_DRAM_PARAMETERS
    ldr r2, =FEL_DRAM_VENDOR_INIT
    blx r2                         /* DDR only: no boot0 main, Flash or UART. */
    mov r6, r0                     /* Vendor return value is capacity in MiB. */

    /* Restore ROM state even when initialization reports failure. */
    ldr r0, =FEL_DRAM_BACKUP
    ldr r1, =FEL_DRAM_VENDOR_BASE
    ldr r2, =(FEL_DRAM_BACKUP + FEL_DRAM_BACKUP_SIZE)
4:
    ldr r3, [r0], #4
    str r3, [r1], #4
    cmp r0, r2
    blo 4b
    bl .Lsync_code
    ldr r0, =FEL_DRAM_MAILBOX
    str r6, [r0, #12]
    mov r1, #FEL_DRAM_INIT_FAILED
    cmp r6, #0
    movne r1, #FEL_DRAM_OK
    ldr r2, =FEL_DRAM_STACK_BOTTOM
    ldr r2, [r2]
    ldr r3, =FEL_DRAM_STACK_CANARY
    cmp r2, r3
    movne r1, #FEL_DRAM_STACK_ERROR
    str r1, [r0, #8]
.Lreturn:
    dsb sy
    mov sp, r5
    msr cpsr_cxsf, r4
    pop {r0-r12, lr}
    bx lr
.Lsync_code:
    dsb sy
    mov r0, #0
    mcr p15, 0, r0, c7, c5, 0
    mcr p15, 0, r0, c7, c5, 6
    dsb sy
    isb sy
    bx lr
.size fel_dram_entry, . - fel_dram_entry
.ltorg

.section .fel_dram_abi, "a", %progbits
.balign 4
    .ascii "R328DDR1"
    .word FEL_DRAM_ENTRY, FEL_DRAM_MAILBOX, FEL_DRAM_VENDOR_STAGE, FEL_DRAM_VENDOR_SIZE
    .word FEL_DRAM_VENDOR_BASE, FEL_DRAM_BACKUP, FEL_DRAM_BACKUP_SIZE
    .word FEL_DRAM_STACK_BOTTOM, FEL_DRAM_STACK_TOP, FEL_DRAM_VENDOR_INIT
    .word FEL_DRAM_MAGIC, FEL_DRAM_ABI
.section .note.GNU-stack, "", %progbits
