#include "example.h"

int main(void)
{
    Example_Init("RCC / clock queries");
    for (;;)
    {
        SystemCoreClockUpdate();
        Example_PrintValue("CPU Hz (0=unknown)=", SystemCoreClock);
        Example_PrintValue("APB2 Hz=", HAL_RCC_GetPCLK2Freq());
        Example_PrintValue("architecture counter Hz=", R328_ARCH_COUNTER_HZ);
        Example_PrintValue("audio PLL4X Hz (0=unknown)=", HAL_RCC_GetAudioPLL4XFreq());
        (void)Board_ConsoleWrite("CPUX_CFG=0x");
        Example_PrintHex(RCC_CPUX_CFG);
        (void)Board_ConsoleWrite(" APB2_CFG=0x");
        Example_PrintHex(RCC_APB2_CFG);
        (void)Board_ConsoleWrite("\r\n");
        HAL_Delay(1000U);
    }
}
