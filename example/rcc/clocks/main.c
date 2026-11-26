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

int main(void)
{
    app_init("RCC / clock queries");
    for (;;)
    {
        SystemCoreClockUpdate();
        HAL_UART_PrintValue(&huart0, "CPU Hz (0=unknown)=", SystemCoreClock, UART_TIMEOUT_MS);
        HAL_UART_PrintValue(&huart0, "APB2 Hz=", HAL_RCC_GetPCLK2Freq(), UART_TIMEOUT_MS);
        HAL_UART_PrintValue(&huart0, "architecture counter Hz=", R328_ARCH_COUNTER_HZ,
                            UART_TIMEOUT_MS);
        HAL_UART_PrintValue(&huart0, "audio PLL4X Hz (0=unknown)=", HAL_RCC_GetAudioPLL4XFreq(),
                            UART_TIMEOUT_MS);
        (void)HAL_UART_PrintString(&huart0, "CPUX_CFG=0x", UART_TIMEOUT_MS);
        HAL_UART_PrintHex(&huart0, RCC_CPUX_CFG, UART_TIMEOUT_MS);
        (void)HAL_UART_PrintString(&huart0, " APB2_CFG=0x", UART_TIMEOUT_MS);
        HAL_UART_PrintHex(&huart0, RCC_APB2_CFG, UART_TIMEOUT_MS);
        (void)HAL_UART_PrintString(&huart0, "\r\n", UART_TIMEOUT_MS);
        HAL_Delay(1000U);
    }
}
