#include "example.h"

int main(void)
{
    Example_Init("CNTPCT / nonblocking deadlines");
    uint32_t fast = HAL_GetTick(), slow = fast, fast_events = 0U, slow_events = 0U;
    (void)Board_ConsoleWrite("250 ms and 1 s jobs run without HAL_Delay; UART keeps echoing.\r\n");
    for (;;)
    {
        uint32_t now = HAL_GetTick();
        if ((uint32_t)(now - fast) >= 250U)
        {
            fast = now;
            ++fast_events;
        }
        if ((uint32_t)(now - slow) >= 1000U)
        {
            slow = now;
            ++slow_events;
            Example_PrintValue("250 ms jobs=", fast_events);
            Example_PrintValue("1 s jobs=", slow_events);
        }
        Example_UART_Poll();
    }
}
