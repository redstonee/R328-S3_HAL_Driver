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
        (void)HAL_UART_PrintValue(&huart0, label, (uint32_t)status);
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
    HAL_UART_SetPollingTimeout(UART_TIMEOUT_MS);
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
    (void)HAL_UART_PrintString(&huart0, "\r\nR328 example: ");
    (void)HAL_UART_PrintString(&huart0, banner);
    (void)HAL_UART_PrintString(&huart0, "\r\nstatus: 0=OK 1=ERROR 2=BUSY 3=TIMEOUT\r\n");
}

static void uart_poll(void)
{
    uint8_t byte;
    if (HAL_UART_GetRxFifoLevel(&huart0) != 0U && HAL_UART_Receive(&huart0, &byte, 1U) == HAL_OK)
    {
        (void)HAL_UART_Transmit(&huart0, &byte, 1U);
    }
}

int main(void)
{
    app_init("CNTPCT / nonblocking deadlines");
    uint32_t fast = HAL_GetTick(), slow = fast, fast_events = 0U, slow_events = 0U;
    (void)HAL_UART_PrintString(
        &huart0, "250 ms and 1 s jobs run without HAL_Delay; UART keeps echoing.\r\n");
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
            HAL_UART_PrintValue(&huart0, "250 ms jobs=", fast_events);
            HAL_UART_PrintValue(&huart0, "1 s jobs=", slow_events);
        }
        uart_poll();
    }
}
