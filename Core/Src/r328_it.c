#include "r328_it.h"
#include "r328_hal.h"

volatile R328_FaultRecord g_fault_record __attribute__((section(".noinit")));
volatile uint32_t g_unhandled_irq = UINT32_MAX;

void IRQ_Handler(void)
{
    HAL_GIC_IRQHandler();
}
void HAL_GIC_UnhandledIRQ(uint32_t irq)
{
    g_unhandled_irq = irq;
}

void Fault_Handler(uint32_t exception, uint32_t pc, uint32_t cpsr)
{
    R328_IRQ_Disable();
    uint32_t dfsr, dfar, ifsr, ifar;
    __asm volatile("mrc p15, 0, %0, c5, c0, 0" : "=r"(dfsr));
    __asm volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(dfar));
    __asm volatile("mrc p15, 0, %0, c5, c0, 1" : "=r"(ifsr));
    __asm volatile("mrc p15, 0, %0, c6, c0, 2" : "=r"(ifar));
    g_fault_record.Exception = exception;
    g_fault_record.PC = pc;
    g_fault_record.CPSR = cpsr;
    g_fault_record.DFSR = dfsr;
    g_fault_record.DFAR = dfar;
    g_fault_record.IFSR = ifsr;
    g_fault_record.IFAR = ifar;
    g_fault_record.Magic = 0x52333238U;
    R328_DSB();
    for (;;)
    {
        R328_WFI();
    }
}
