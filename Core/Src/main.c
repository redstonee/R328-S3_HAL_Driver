#include "main.h"
#include "board.h"

static TIM_HandleTypeDef htim1;
volatile uint32_t g_timer1_events;
volatile uint32_t g_application_error;

static void timer_elapsed(TIM_HandleTypeDef *handle)
{
    UNUSED(handle);
    ++g_timer1_events; /* IRQ callback: avoid delays and blocking UART operations. */
}
static void MX_TIM1_Init(void)
{
    htim1.Instance = TIM1;
    htim1.Init.Prescaler = TIM_PRESCALER_128;
    htim1.Init.Period = R328_OSC24M_HZ / 128U;
    htim1.Init.Mode = TIM_MODE_CONTINUOUS;
    htim1.PeriodElapsedCallback = timer_elapsed;
    if (HAL_TIM_Base_Init(&htim1) != HAL_OK || HAL_TIM_Base_Start_IT(&htim1) != HAL_OK)
    {
        Error_Handler();
    }
}
void SystemClock_Config(void)
{
    if (HAL_RCC_APB2_Config24M() != HAL_OK)
    {
        Error_Handler();
    }
    SystemCoreClockUpdate();
}
int main(void)
{
    if (HAL_Init() != HAL_OK)
    {
        Error_Handler();
    }
    SystemClock_Config();
    if (Board_Init() != HAL_OK)
    {
        Error_Handler();
    }
    MX_TIM1_Init();
    (void)Board_ConsoleWrite(
        "\r\nR328-S3 HAL / Cortex-A7 / CMake\r\nUART0 115200 8N1; echo enabled\r\n");
    uint32_t last = HAL_GetTick();
    for (;;)
    {
        uint8_t byte;
        if (HAL_UART_Receive(&huart0, &byte, 1U, 0U) == HAL_OK)
        {
            (void)HAL_UART_Transmit(&huart0, &byte, 1U, 100U);
        }
        uint32_t now = HAL_GetTick();
        if ((uint32_t)(now - last) >= 1000U)
        {
            last = now;
            Board_LED_Toggle();
            (void)Board_ConsoleWrite("tick=");
            (void)Board_ConsoleWriteU32(now);
            (void)Board_ConsoleWrite(" timer1=");
            (void)Board_ConsoleWriteU32(g_timer1_events);
            (void)Board_ConsoleWrite("\r\n");
        }
        /* UART uses polling. No periodic HAL tick IRQ exists to wake WFI;
         * an interrupt-driven idle loop needs its own wakeup source. */
    }
}
void Error_Handler(void)
{
    R328_IRQ_Disable();
    g_application_error = 1U;
    for (;;)
    {
        R328_WFI();
    }
}
