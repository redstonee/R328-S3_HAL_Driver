#include "example.h"

#define DRAM_BASE 0x40000000U
#define DRAM_END 0x48000000U

extern uint32_t __image_start__;
extern uint32_t __static_end__;
extern uint32_t __stack_bottom__;
extern uint32_t __stack_top__;

static void PrintHex(const char *label, uint32_t value)
{
    (void)Board_ConsoleWrite(label);
    (void)Board_ConsoleWrite("0x");
    Example_PrintHex(value);
    (void)Board_ConsoleWrite("\r\n");
}

static void Fail(uintptr_t address, uint32_t expected, uint32_t actual)
{
    PrintHex("DRAM FAIL address=", (uint32_t)address);
    PrintHex("expected=", expected);
    PrintHex("actual=", actual);
    Example_Idle();
}

static void TestDataBus(uintptr_t address)
{
    volatile uint32_t *word = (volatile uint32_t *)address;
    /* Walk both a single 1 and a single 0 through all 32 data bits. */
    for (uint32_t bit = 1U; bit != 0U; bit <<= 1U)
    {
        for (uint32_t invert = 0U; invert < 2U; ++invert)
        {
            uint32_t expected = invert == 0U ? bit : ~bit;
            *word = expected;
            R328_DSB();
            uint32_t actual = *word;
            if (actual != expected)
            {
                Fail(address, expected, actual);
            }
        }
    }
    (void)Board_ConsoleWrite("data bus PASS (walking 1/0)\r\n");
}

static void TestRange(uintptr_t begin, uintptr_t end, uint32_t pattern, uint32_t use_address)
{
    /* Write the whole region before verifying it: testing small chunks
     * independently would miss aliases between different chunks. */
    for (uintptr_t address = begin; address < end; address += sizeof(uint32_t))
    {
        uint32_t expected = pattern ^ (use_address != 0U ? (uint32_t)address : 0U);
        *(volatile uint32_t *)address = expected;
    }
    R328_DSB();
    for (uintptr_t address = begin; address < end; address += sizeof(uint32_t))
    {
        uint32_t expected = pattern ^ (use_address != 0U ? (uint32_t)address : 0U);
        uint32_t actual = *(volatile uint32_t *)address;
        if (actual != expected)
        {
            Fail(address, expected, actual);
        }
    }
    R328_DSB();
}

static void TestMemory(void)
{
    uintptr_t begin = ((uintptr_t)&__static_end__ + 63U) & ~(uintptr_t)63U;
    uintptr_t end = (uintptr_t)&__stack_bottom__ & ~(uintptr_t)3U;
    uintptr_t origin = (uintptr_t)&__image_start__;
    uintptr_t top = (uintptr_t)&__stack_top__;
    uint32_t control;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(control));
    if (origin < DRAM_BASE || origin >= DRAM_END || begin < origin || end > top || top > DRAM_END ||
        begin >= end)
    {
        (void)Board_ConsoleWrite("DRAM SKIP: use a DDR build with initialized DRAM\r\n");
        Example_Idle();
    }
    if ((control & 5U) != 0U)
    {
        (void)Board_ConsoleWrite("DRAM SKIP: MMU and D-cache must be disabled\r\n");
        Example_Idle();
    }

    PrintHex("begin=", (uint32_t)begin);
    PrintHex("end (exclusive)=", (uint32_t)end);
    Example_PrintValue("tested bytes=", (uint32_t)(end - begin));
    uint32_t start = HAL_GetTick();
    TestDataBus(begin);
    static const uint32_t patterns[] = {0U, 0xFFFFFFFFU, 0x55555555U, 0xAAAAAAAAU, 0U, 0xFFFFFFFFU};
    static const char *const names[] = {"zero", "ones", "55", "AA", "address", "~address"};
    for (uint32_t pass = 0U; pass < sizeof(patterns) / sizeof(patterns[0]); ++pass)
    {
        (void)Board_ConsoleWrite("testing ");
        (void)Board_ConsoleWrite(names[pass]);
        (void)Board_ConsoleWrite("...\r\n");
        uint32_t pass_start = HAL_GetTick();
        TestRange(begin, end, patterns[pass], pass >= 4U);
        Example_PrintValue("PASS ms=", (uint32_t)(HAL_GetTick() - pass_start));
    }
    (void)Board_ConsoleWrite("DRAM PASS (free region, 6 patterns + data bus)\r\n");
    Example_PrintValue("total ms=", (uint32_t)(HAL_GetTick() - start));
}

int main(void)
{
    Example_Init("DRAM free-memory test");
    TestMemory();
    for (;;)
    {
        HAL_Delay(1000U);
        Example_PrintValue("tick=", HAL_GetTick());
    }
}
