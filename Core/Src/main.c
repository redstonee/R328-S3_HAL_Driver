#include "main.h"

volatile uint32_t g_application_error;

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
    /* Configure application peripherals and their pins here. */
    GPIO_InitTypeDef gInit = {.Pin = LED1_PIN, .Mode = GPIO_MODE_OUTPUT_PP};
    HAL_GPIO_Init(LED1_PORT, &gInit);
    gInit.Pin = LED2_PIN;
    HAL_GPIO_Init(LED2_PORT, &gInit);

    gInit.Pin = UART0_TX_PIN | UART0_RX_PIN;
    gInit.Mode = GPIO_MODE_AF_PP;
    gInit.Pull = GPIO_PULLUP;
    gInit.Drive = GPIO_DRIVE_LEVEL1;
    gInit.Alternate = 3;
    HAL_GPIO_Init(UART0_PORT, &gInit);

    UART_InitTypeDef uart_init = {115200U, UART_WORDLENGTH_8B, UART_STOPBITS_1, UART_PARITY_NONE};
    UART_HandleTypeDef huart0 = {.Instance = UART0, .Init = uart_init};
    HAL_UART_Init(&huart0);

    for (;;)
    {
        HAL_UART_PrintString(&huart0, "Fuck The World! ", 100);
        HAL_UART_PrintU32(&huart0, HAL_GetTick(), 100);
        HAL_UART_PrintString(&huart0, "\r\n", 100);

        HAL_GPIO_TogglePin(LED1_PORT, LED1_PIN);
        HAL_Delay(500);
        HAL_GPIO_TogglePin(LED2_PORT, LED2_PIN);
        HAL_Delay(500);
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
