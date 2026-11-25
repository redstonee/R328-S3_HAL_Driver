#include "example.h"

extern uint32_t __static_end__;
extern uint32_t __stack_bottom__;
extern uint32_t __stack_top__;
extern uint32_t __image_start__;

static void PrintAddress(const char *label, uintptr_t address)
{
    (void)Board_ConsoleWrite(label);
    (void)Board_ConsoleWrite("0x");
    Example_PrintHex((uint32_t)address);
    (void)Board_ConsoleWrite("\r\n");
}

static void TestMemory(void)
{
    uintptr_t begin = (uintptr_t)&__static_end__;
    uintptr_t end = (uintptr_t)&__stack_bottom__;
    uint32_t start = HAL_GetTick();
    PrintAddress("free begin=", begin);
    PrintAddress("free end (exclusive)=", end);
    Example_PrintValue("tested bytes=", (uint32_t)(end - begin));
    /* Fill the entire free region before reading, to expose address aliasing.
     * Code, statics and every exception stack remain outside the test region.
     */
    for (uint32_t pass = 0U; pass < 2U; ++pass)
    {
        uint32_t pattern = pass == 0U ? 0x55AA33CCU : 0xAA55CC33U;
        for (uintptr_t p = begin; p < end; p += sizeof(uint32_t))
        {
            *(volatile uint32_t *)p = (uint32_t)p ^ pattern;
        }
        __asm__ volatile("dsb sy" ::: "memory");
        for (uintptr_t p = begin; p < end; p += sizeof(uint32_t))
        {
            uint32_t expected = (uint32_t)p ^ pattern;
            uint32_t actual = *(volatile uint32_t *)p;
            if (actual != expected)
            {
                PrintAddress("SRAM FAIL address=", p);
                PrintAddress("expected=", expected);
                PrintAddress("actual=", actual);
                Example_Idle();
            }
        }
    }
    (void)Board_ConsoleWrite("SRAM PASS (address pattern + inverse)\r\n");
    Example_PrintValue("test ms=", (uint32_t)(HAL_GetTick() - start));
}

int main(void)
{
    Example_Init("SRAM free-memory test");
    PrintAddress("runtime origin=", (uintptr_t)&__image_start__);
    PrintAddress("SRAM top=", (uintptr_t)&__stack_top__);
    TestMemory();
    for (;;)
    {
        HAL_Delay(1000U);
        Example_PrintValue("tick=", HAL_GetTick());
    }
}
