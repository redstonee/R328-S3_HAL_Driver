#include "r328.h"

uint32_t SystemCoreClock = R328_OSC24M_HZ;

void SystemCoreClockUpdate(void)
{
    uint32_t source = (RCC_CPUX_CFG >> 24) & 7U;
    if (source == 0U)
    {
        SystemCoreClock = R328_OSC24M_HZ;
    }
    else if (source == 3U)
    {
        uint32_t pll = R328_CCU_REG(0x0000U);
        uint32_t p = (pll >> 16) & 3U;
        /* Manual 3.3.5.1: PLL_CPUX = input * (N+1) / P.
           Nonzero M is not covered by the documented formula: report unknown. */
        uint64_t frequency = (uint64_t)R328_OSC24M_HZ * (((pll >> 8) & 0xFFU) + 1U);
        frequency >>= p;
        SystemCoreClock =
            (p < 3U && (pll & 3U) == 0U && (pll & (1UL << 31)) != 0U && frequency <= UINT32_MAX)
                ? (uint32_t)frequency
                : 0U;
    }
    else if (source == 4U)
    {
        uint32_t pll = R328_CCU_REG(0x0020U);
        uint64_t frequency = (uint64_t)R328_OSC24M_HZ * (((pll >> 8) & 0xFFU) + 1U);
        frequency /= ((pll & 1U) + 1U) * (((pll >> 1) & 1U) + 1U) * 2U;
        SystemCoreClock = (pll & (1UL << 31)) != 0U ? (uint32_t)frequency : 0U;
    }
    else
    {
        SystemCoreClock = 0U; /* Unknown/board-specific RC or low-frequency source. */
    }
}
void SystemInit(void)
{
    /* Preserve DDR, LDO voltages, PLLs and SRAM/VAD setup established by the loader. */
    SystemCoreClockUpdate();
}
