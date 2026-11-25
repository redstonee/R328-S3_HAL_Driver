#include "example.h"

static uint32_t pixels[BOARD_WS2812_LED_COUNT];

static void Show(void)
{
    HAL_StatusTypeDef status = HAL_LEDC_Transmit(&hledc, pixels, BOARD_WS2812_LED_COUNT, 100U);
    if (status != HAL_OK)
    {
        Example_PrintValue("LEDC error=", hledc.ErrorCode);
        Example_RequireOK("LEDC transmit=", status);
    }
}

static void Fill(uint8_t red, uint8_t green, uint8_t blue)
{
    for (uint32_t i = 0U; i < BOARD_WS2812_LED_COUNT; ++i)
    {
        pixels[i] = HAL_LEDC_ColorRGB(red, green, blue);
    }
    Show();
}

int main(void)
{
    Example_Init("LEDC / WS2812, PE2 AF2, 4 pixels");
    Example_RequireOK("LEDC init=", Board_LEDC_Init(BOARD_WS2812_LED_COUNT));
    (void)Board_ConsoleWrite("pixel order: LED7 -> LED6 -> LED5 -> LED4\r\n");
    Fill(0U, 0U, 0U);
    for (;;)
    {
        (void)Board_ConsoleWrite("red / green / blue / white\r\n");
        Fill(16U, 0U, 0U);
        HAL_Delay(500U);
        Fill(0U, 16U, 0U);
        HAL_Delay(500U);
        Fill(0U, 0U, 16U);
        HAL_Delay(500U);
        Fill(8U, 8U, 8U);
        HAL_Delay(500U);

        (void)Board_ConsoleWrite("chase\r\n");
        for (uint32_t i = 0U; i < BOARD_WS2812_LED_COUNT; ++i)
        {
            Fill(0U, 0U, 0U);
            pixels[i] = HAL_LEDC_ColorRGB(0U, 16U, 8U);
            Show();
            HAL_Delay(250U);
        }
        (void)Board_ConsoleWrite("blue breathing\r\n");
        for (uint32_t step = 0U; step <= 64U; ++step)
        {
            uint32_t brightness = step <= 32U ? step : 64U - step;
            Fill(0U, 0U, (uint8_t)brightness);
            HAL_Delay(20U);
        }
        Example_PrintValue("tick=", HAL_GetTick());
        Example_UART_Poll();
    }
}
