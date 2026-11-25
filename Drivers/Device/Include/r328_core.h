#ifndef R328_CORE_H
#define R328_CORE_H

#include <stdint.h>

#if defined(R328_HAL_HOST_TEST)
/* Host tests exercise register logic only, never CPU instructions. */
static inline uint32_t R328_IRQ_Save(void)
{
    return 0U;
}
static inline void R328_IRQ_Restore(uint32_t state)
{
    (void)state;
}
static inline void R328_IRQ_Enable(void) {}
static inline void R328_IRQ_Disable(void) {}
static inline void R328_DSB(void) {}
static inline void R328_WFI(void) {}
uint64_t R328_Test_ArchCounter_Read(void);
static inline uint64_t R328_ArchCounter_Read(void)
{
    return R328_Test_ArchCounter_Read();
}
#else
static inline uint32_t R328_IRQ_Save(void)
{
    uint32_t state;
    __asm volatile("mrs %0, cpsr\n\tcpsid i" : "=r"(state)::"memory", "cc");
    return state;
}
static inline void R328_IRQ_Restore(uint32_t state)
{
    if ((state & 0x80U) == 0U)
    {
        __asm volatile("cpsie i" ::: "memory", "cc");
    }
}
static inline void R328_IRQ_Enable(void)
{
    __asm volatile("cpsie i" ::: "memory", "cc");
}
static inline void R328_IRQ_Disable(void)
{
    __asm volatile("cpsid i" ::: "memory", "cc");
}
static inline void R328_DSB(void)
{
    __asm volatile("dsb sy" ::: "memory");
}
static inline void R328_WFI(void)
{
    __asm volatile("wfi" ::: "memory");
}
/* CNTPCT: one coherent 64-bit read. ISB orders the sample after prior instructions. */
static inline uint64_t R328_ArchCounter_Read(void)
{
    uint32_t low, high;
    __asm volatile("isb\n\tmrrc p15, 0, %0, %1, c14" : "=r"(low), "=r"(high)::"memory");
    return ((uint64_t)high << 32) | low;
}
#endif
#endif
