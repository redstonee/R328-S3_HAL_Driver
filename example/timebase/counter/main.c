#include "example.h"

int main(void)
{
    Example_Init("CNTPCT timebase / no timer IRQ");
    for (;;)
    {
        /* Demonstrate that the counter still measures time with IRQs masked. */
        uint32_t state = R328_IRQ_Save();
        uint32_t start = HAL_GetTick();
        HAL_Delay(1000U);
        uint32_t elapsed = (uint32_t)(HAL_GetTick() - start);
        uint64_t counter = R328_ArchCounter_Read();
        R328_IRQ_Restore(state);
        (void)Board_ConsoleWrite("IRQ-masked elapsed ms=");
        (void)Board_ConsoleWriteU32(elapsed);
        (void)Board_ConsoleWrite(" counter=0x");
        Example_PrintHex((uint32_t)(counter >> 32));
        Example_PrintHex((uint32_t)counter);
        (void)Board_ConsoleWrite("\r\n");
    }
}
