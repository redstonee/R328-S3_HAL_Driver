.syntax unified
.cpu cortex-a7
.arm

/* BROM uses BOTH low SRAM and high SRAM (including a stack near 0x3c600).
 * FEL uploads this template into its 512-byte scratchpad at 0x27e00.
 * After abandoning BROM, the prefix moves the position-independent second
 * stage to 0x3ff00. Only then may it copy the image over the low SRAM.
 * Both stages use no stack, calls, or return to BROM. The package tool
 * limits the entire template to 256 bytes, so its second stage also fits
 * the final 256-byte slot at the top of SRAM.
 */
.section .text.fel_relocate, "ax", %progbits
.balign 4
.global fel_relocate_start
.type fel_relocate_start, %function
fel_relocate_start:
    cpsid if
    cps #0x13
    mrc p15, 0, r0, c0, c0, 5
    ands r0, r0, #3
    bne .Lbootstrap_halt
    mrc p15, 0, r0, c1, c0, 0
    tst r0, #5                  /* MMU and D-cache must be disabled. */
    bne .Lbootstrap_halt
    adr r0, .Lrelocate
    movw r1, #0xff00
    movt r1, #0x0003
    mov r4, r1
    mov r2, #(.Lend - .Lrelocate)
.Lbootstrap_copy:
    ldrb r3, [r0], #1
    strb r3, [r1], #1
    subs r2, r2, #1
    bne .Lbootstrap_copy
    dsb sy
    mov r0, #0
    mcr p15, 0, r0, c7, c5, 0
    mcr p15, 0, r0, c7, c5, 6
    dsb sy
    isb sy
    bx r4
.Lbootstrap_halt:
    wfi
    b .Lbootstrap_halt
.Lrelocate:
    adr r4, .Lparams
    ldmia r4, {r0, r1, r2}
    mov r4, r1
    cmp r2, #0
    beq .Lhalt
.Lcopy:
    ldrb r3, [r0], #1
    strb r3, [r1], #1
    subs r2, r2, #1
    bne .Lcopy
    dsb sy
    mov r0, #0
    mcr p15, 0, r0, c7, c5, 0  /* Invalidate instruction cache. */
    mcr p15, 0, r0, c7, c5, 6  /* Invalidate branch predictor. */
    dsb sy
    isb sy
    bx r4
.Lhalt:
    wfi
    b .Lhalt
.balign 4
    .ascii "R328"
.Lparams:
    .word 0x00028000
    .word 0x00020000
    .word 0
.Lend:
.size fel_relocate_start, . - fel_relocate_start
.section .note.GNU-stack, "", %progbits
