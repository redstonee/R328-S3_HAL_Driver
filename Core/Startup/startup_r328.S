.syntax unified
.cpu cortex-a7
.arm

.section .vectors, "ax", %progbits
.balign 32
.global _start
.type _start, %function
_start:
    b Reset_Handler
    b Undefined_Handler
    b SVC_Handler
    b PrefetchAbort_Handler
    b DataAbort_Handler
    b Reserved_Handler
    b IRQ_Entry
    b FIQ_Handler
.size _start, . - _start

.section .text.startup, "ax", %progbits
.global Reset_Handler
.type Reset_Handler, %function
Reset_Handler:
    cpsid if
#if defined(R328_BOOT_FEL)
    /* Take over from BROM permanently; do not keep or return to its stack.
       CPS selects SVC in the current (BROM secure) world before our stacks. */
    cps #0x13
#endif
    mrc p15, 0, r0, c0, c0, 5
    ands r0, r0, #3
    bne startup_halt                 /* CPU1 must never share CPU0 stacks. */
    mrs r0, cpsr
    and r0, r0, #0x1f
    cmp r0, #0x13                    /* Boot contract: privileged SVC mode. */
    bne startup_halt
    mrc p15, 0, r0, c1, c0, 0
    tst r0, #5                       /* Loader must clean/disable D-cache and MMU. */
    bne startup_halt
    bic r0, r0, #(1 << 12)           /* I-cache */
    bic r0, r0, #(1 << 11)           /* branch prediction */
    bic r0, r0, #(1 << 13)           /* VBAR vectors */
    bic r0, r0, #(1 << 25)           /* little-endian exceptions */
    bic r0, r0, #(1 << 30)           /* ARM exception handlers */
    mcr p15, 0, r0, c1, c0, 0
    dsb sy
    isb sy
    mov r0, #0
    mcr p15, 0, r0, c7, c5, 0       /* invalidate I-cache */
    mcr p15, 0, r0, c7, c5, 6       /* invalidate branch predictor */
    dsb sy
    isb sy
    ldr r0, =_start
    mcr p15, 0, r0, c12, c0, 0
    isb sy

    cps #0x12
    ldr sp, =__irq_stack_top__
    cps #0x11
    ldr sp, =__fiq_stack_top__
    cps #0x17
    ldr sp, =__abt_stack_top__
    cps #0x1b
    ldr sp, =__und_stack_top__
    cps #0x13
    ldr sp, =__svc_stack_top__

    ldr r0, =__data_load__
    ldr r1, =__data_start__
    ldr r2, =__data_end__
1:
    cmp r1, r2
    ldrlo r3, [r0], #4
    strlo r3, [r1], #4
    blo 1b
    ldr r1, =__bss_start__
    ldr r2, =__bss_end__
    mov r3, #0
2:
    cmp r1, r2
    strlo r3, [r1], #4
    blo 2b
    bl SystemInit
    bl main
.global startup_halt
startup_halt:
    cpsid if
3:
    wfi
    b 3b
.size Reset_Handler, . - Reset_Handler

.global IRQ_Entry
.type IRQ_Entry, %function
IRQ_Entry:
    sub lr, lr, #4
    stmdb sp!, {r0-r12, lr}          /* 56 bytes, preserve interrupted context. */
    mrs r0, spsr
    push {r0, r1}                   /* SPSR + padding: keep C stack 8-byte aligned. */
    bl IRQ_Handler
    pop {r0, r1}
    msr spsr_cxsf, r0
    ldmia sp!, {r0-r12, pc}^
.size IRQ_Entry, . - IRQ_Entry

.macro FAULT_ENTRY name, number, offset
.type \name, %function
\name:
    mov r0, #\number
    sub r1, lr, #\offset
    mrs r2, spsr
    bl Fault_Handler
    b startup_halt
.size \name, . - \name
.endm
FAULT_ENTRY Undefined_Handler, 1, 4
FAULT_ENTRY SVC_Handler, 2, 4
FAULT_ENTRY PrefetchAbort_Handler, 3, 4
FAULT_ENTRY DataAbort_Handler, 4, 8
FAULT_ENTRY Reserved_Handler, 5, 4
FAULT_ENTRY FIQ_Handler, 6, 4

.section .note.GNU-stack, "", %progbits
