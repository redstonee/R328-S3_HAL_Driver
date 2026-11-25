#include "example.h"

volatile uint32_t g_application_error;

void SystemClock_Config(void)
{
    if (HAL_RCC_APB2_Config24M() != HAL_OK)
    {
        Error_Handler();
    }
    SystemCoreClockUpdate();
}
void Example_Init(const char *banner)
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
    (void)Board_ConsoleWrite("\r\nR328 example: ");
    (void)Board_ConsoleWrite(banner);
    (void)Board_ConsoleWrite("\r\nstatus: 0=OK 1=ERROR 2=BUSY 3=TIMEOUT\r\n");
}
void Example_Report(const char *label, HAL_StatusTypeDef status)
{
    (void)Board_ConsoleWrite(label);
    (void)Board_ConsoleWriteU32((uint32_t)status);
    (void)Board_ConsoleWrite("\r\n");
}
void Example_RequireOK(const char *label, HAL_StatusTypeDef status)
{
    if (status != HAL_OK)
    {
        Example_Report(label, status);
        Error_Handler();
    }
}
void Example_PrintValue(const char *label, uint32_t value)
{
    (void)Board_ConsoleWrite(label);
    (void)Board_ConsoleWriteU32(value);
    (void)Board_ConsoleWrite("\r\n");
}
void Example_PrintHex(uint32_t value)
{
    static const char digits[] = "0123456789ABCDEF";
    uint8_t text[8];
    for (uint32_t i = 0U; i < sizeof(text); ++i)
    {
        text[i] = (uint8_t)digits[(value >> ((7U - i) * 4U)) & 0xFU];
    }
    (void)HAL_UART_Transmit(&huart0, text, sizeof(text), 100U);
}
void Example_Dump(const uint8_t *data, uint32_t size)
{
    static const char digits[] = "0123456789ABCDEF";
    for (uint32_t i = 0U; i < size; ++i)
    {
        uint8_t text[] = {(uint8_t)digits[data[i] >> 4], (uint8_t)digits[data[i] & 0xFU], ' '};
        (void)HAL_UART_Transmit(&huart0, text, sizeof(text), 100U);
    }
    (void)Board_ConsoleWrite("\r\n");
}
void Example_UART_Poll(void)
{
    uint8_t byte;
    if (HAL_UART_Receive(&huart0, &byte, 1U, 0U) == HAL_OK)
    {
        (void)HAL_UART_Transmit(&huart0, &byte, 1U, 100U);
    }
}
void Example_Idle(void)
{
    /* Polling UART has no RX IRQ; the counter alone cannot wake WFI. */
    for (;;)
    {
        Example_UART_Poll();
    }
}
void Error_Handler(void)
{
    g_application_error = 1U;
    R328_IRQ_Disable();
    for (;;)
    {
        R328_WFI();
    }
}
