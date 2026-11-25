#include "board.h"

UART_HandleTypeDef huart0;

HAL_StatusTypeDef Board_Init(void)
{
    huart0.Instance = UART0;
    huart0.Init.BaudRate = 115200U;
    huart0.Init.WordLength = UART_WORDLENGTH_8B;
    huart0.Init.StopBits = UART_STOPBITS_1;
    huart0.Init.Parity = UART_PARITY_NONE;
    HAL_StatusTypeDef status = HAL_UART_Init(&huart0);
    if (status != HAL_OK)
    {
        return status;
    }
#if BOARD_LED_ENABLE
    HAL_GPIO_WritePin(BOARD_LED_PORT, BOARD_LED_PIN, GPIO_PIN_RESET);
    GPIO_InitTypeDef init = {
        .Pin = BOARD_LED_PIN, .Mode = GPIO_MODE_OUTPUT_PP, .Drive = GPIO_DRIVE_LEVEL0};
    return HAL_GPIO_Init(BOARD_LED_PORT, &init);
#else
    return HAL_OK;
#endif
}
HAL_StatusTypeDef Board_ConsoleWrite(const char *text)
{
    if (text == NULL)
    {
        return HAL_ERROR;
    }
    uint32_t length = 0U;
    while (text[length] != '\0')
    {
        ++length;
    }
    return HAL_UART_Transmit(&huart0, (const uint8_t *)text, length, 100U);
}
HAL_StatusTypeDef Board_ConsoleWriteU32(uint32_t value)
{
    uint8_t buffer[10];
    uint32_t offset = sizeof(buffer);
    do
    {
        buffer[--offset] = (uint8_t)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);
    return HAL_UART_Transmit(&huart0, &buffer[offset], sizeof(buffer) - offset, 100U);
}
void Board_LED_Toggle(void)
{
#if BOARD_LED_ENABLE
    HAL_GPIO_TogglePin(BOARD_LED_PORT, BOARD_LED_PIN);
#endif
}
