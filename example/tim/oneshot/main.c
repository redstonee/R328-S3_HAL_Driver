#include "example.h"

static TIM_HandleTypeDef timer;
static volatile uint32_t events;

static void elapsed(TIM_HandleTypeDef *handle)
{
    UNUSED(handle);
    ++events;
}

int main(void)
{
    Example_Init("Timer1 / 250 ms one-shot and explicit rearm");
    timer.Instance = TIM1;
    timer.Init = (TIM_Base_InitTypeDef){.Prescaler = TIM_PRESCALER_128,
                                        .Period = R328_OSC24M_HZ / 128U / 4U,
                                        .Mode = TIM_MODE_ONESHOT};
    timer.PeriodElapsedCallback = elapsed;
    Example_RequireOK("Timer init=", HAL_TIM_Base_Init(&timer));
    uint32_t started = HAL_GetTick();
    Example_RequireOK("Timer start=", HAL_TIM_Base_Start_IT(&timer));
    uint32_t reported = 0U;
    (void)Board_ConsoleWrite("One-shot armed; press s after expiry to arm again.\r\n");
    for (;;)
    {
        uint32_t count = events;
        if (count != reported)
        {
            reported = count;
            Example_PrintValue("one-shot events=", count);
            Example_PrintValue("elapsed ms=", (uint32_t)(HAL_GetTick() - started));
        }
        uint8_t byte;
        if (HAL_UART_Receive(&huart0, &byte, 1U, 0U) == HAL_OK && byte == 's')
        {
            if (timer.State == HAL_TIM_STATE_READY)
            {
                Example_RequireOK("Timer stop=", HAL_TIM_Base_Stop_IT(&timer));
                started = HAL_GetTick();
                Example_RequireOK("Timer rearm=", HAL_TIM_Base_Start_IT(&timer));
            }
            else
            {
                (void)Board_ConsoleWrite("Timer still running.\r\n");
            }
        }
        /* Keep polling: a completed one-shot no longer provides periodic wakeups. */
    }
}
