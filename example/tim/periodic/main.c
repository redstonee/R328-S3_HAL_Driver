#include "example.h"

static TIM_HandleTypeDef timer;
volatile uint32_t g_timer1_events;
static void timer_elapsed(TIM_HandleTypeDef *handle)
{
    UNUSED(handle);
    ++g_timer1_events;
}
int main(void)
{
    Example_Init("Timer1 / 1 second IRQ");
    timer.Instance = TIM1;
    timer.Init = (TIM_Base_InitTypeDef){.Prescaler = TIM_PRESCALER_128,
                                        .Period = R328_OSC24M_HZ / 128U,
                                        .Mode = TIM_MODE_CONTINUOUS};
    timer.PeriodElapsedCallback = timer_elapsed;
    HAL_StatusTypeDef status = HAL_TIM_Base_Init(&timer);
    if (status == HAL_OK)
    {
        status = HAL_TIM_Base_Start_IT(&timer);
    }
    Example_Report("Timer1 init/start=", status);
    if (status != HAL_OK)
    {
        Error_Handler();
    }
    uint32_t last = 0U;
    for (;;)
    {
        uint32_t events = g_timer1_events;
        if (events != last)
        {
            last = events;
            (void)Board_ConsoleWrite("timer1=");
            (void)Board_ConsoleWriteU32(events);
            (void)Board_ConsoleWrite(" tick=");
            (void)Board_ConsoleWriteU32(HAL_GetTick());
            (void)Board_ConsoleWrite("\r\n");
        }
        R328_WFI(); /* This example has an explicit periodic wakeup source. */
    }
}
