#include "main.h"

#define UART_TIMEOUT_MS 100U
static UART_HandleTypeDef huart0;
volatile uint32_t g_application_error;

void Error_Handler(void)
{
    g_application_error = 1U;
    R328_IRQ_Disable();
    for (;;)
    {
        R328_WFI();
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
static void require_ok(const char *label, HAL_StatusTypeDef status)
{
    if (status != HAL_OK)
    {
        (void)HAL_UART_PrintValue(&huart0, label, (uint32_t)status, UART_TIMEOUT_MS);
        Error_Handler();
    }
}
static void app_init(const char *banner)
{
    if (HAL_Init() != HAL_OK)
    {
        Error_Handler();
    }
    SystemClock_Config();
    /* This example explicitly chooses UART0 PH0/PH1, 115200 8N1 for diagnostics.
     * The framework itself does not reserve any UART or initialize these pins. */
    GPIO_InitTypeDef uart_pins = {.Pin = GPIO_PIN_0 | GPIO_PIN_1,
                                  .Mode = GPIO_MODE_AF_PP,
                                  .Pull = GPIO_PULLUP,
                                  .Drive = GPIO_DRIVE_LEVEL1,
                                  .Alternate = 3U};
    if (HAL_GPIO_Init(GPIOH, &uart_pins) != HAL_OK)
    {
        Error_Handler();
    }
    huart0.Instance = UART0;
    huart0.Init =
        (UART_InitTypeDef){115200U, UART_WORDLENGTH_8B, UART_STOPBITS_1, UART_PARITY_NONE};
    require_ok("UART init=", HAL_UART_Init(&huart0));
    (void)HAL_UART_PrintString(&huart0, "\r\nR328 example: ", UART_TIMEOUT_MS);
    (void)HAL_UART_PrintString(&huart0, banner, UART_TIMEOUT_MS);
    (void)HAL_UART_PrintString(&huart0, "\r\nstatus: 0=OK 1=ERROR 2=BUSY 3=TIMEOUT\r\n",
                               UART_TIMEOUT_MS);
}

static TIM_HandleTypeDef timer;
volatile uint32_t g_timer1_events;
static void timer_elapsed(TIM_HandleTypeDef *handle)
{
    UNUSED(handle);
    ++g_timer1_events;
}
int main(void)
{
    app_init("Timer1 / 1 second IRQ");
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
    HAL_UART_PrintValue(&huart0, "Timer1 init/start=", status, UART_TIMEOUT_MS);
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
            (void)HAL_UART_PrintString(&huart0, "timer1=", UART_TIMEOUT_MS);
            (void)HAL_UART_PrintU32(&huart0, events, UART_TIMEOUT_MS);
            (void)HAL_UART_PrintString(&huart0, " tick=", UART_TIMEOUT_MS);
            (void)HAL_UART_PrintU32(&huart0, HAL_GetTick(), UART_TIMEOUT_MS);
            (void)HAL_UART_PrintString(&huart0, "\r\n", UART_TIMEOUT_MS);
        }
        R328_WFI(); /* This example has an explicit periodic wakeup source. */
    }
}
