#include "example.h"
#include "ili9341.h"
#include "xpt2046.h"
#include "display_bus.h"

static void require_ok(const char *label, HAL_StatusTypeDef status)
{
    if (status != HAL_OK)
    {
        Example_Report(label, status);
        Example_PrintValue("SPI ErrorCode=", hspi1.ErrorCode);
        ILI9341_Backlight(0U);
        Error_Handler();
    }
}
static void color_bars(void)
{
    static const uint16_t colors[] = {ILI9341_WHITE, 0xFFE0U,     0x07FFU,      ILI9341_GREEN,
                                      0xF81FU,       ILI9341_RED, ILI9341_BLUE, ILI9341_BLACK};
    for (uint32_t i = 0U; i < 8U; ++i)
    {
        require_ok("LCD bars=",
                   ILI9341_FillRect((uint16_t)(i * 30U), 0U, 30U, ILI9341_HEIGHT, colors[i]));
    }
    require_ok("LCD border=", ILI9341_FillRect(0U, 0U, ILI9341_WIDTH, 2U, ILI9341_WHITE));
    require_ok("LCD border=",
               ILI9341_FillRect(0U, ILI9341_HEIGHT - 2U, ILI9341_WIDTH, 2U, ILI9341_WHITE));
    require_ok("LCD border=", ILI9341_FillRect(0U, 0U, 2U, ILI9341_HEIGHT, ILI9341_WHITE));
    require_ok("LCD border=",
               ILI9341_FillRect(ILI9341_WIDTH - 2U, 0U, 2U, ILI9341_HEIGHT, ILI9341_WHITE));
}
int main(void)
{
    Example_Init("ILI9341 + XPT2046 / shared SPI1 / per-device clock / mode 0");
    require_ok("SPI DMA init=", Display_SPI_Init());
    HAL_StatusTypeDef status = ILI9341_Init();
    Example_Report("ILI9341 init=", status);
    require_ok("ILI9341 failed=", status);
    Example_PrintValue("LCD actual SPI Hz=", hspi1.ActualBaudRateHz);
    status = XPT2046_Init();
    Example_Report("XPT2046 init=", status);
    require_ok("XPT2046 failed=", status);
    Example_PrintValue("Touch actual SPI Hz=", hspi1.ActualBaudRateHz);

    static const uint16_t fills[] = {ILI9341_RED, ILI9341_GREEN, ILI9341_BLUE};
    for (uint32_t i = 0U; i < sizeof(fills) / sizeof(fills[0]); ++i)
    {
        require_ok("LCD fill=", ILI9341_Fill(fills[i]));
        HAL_Delay(700U);
    }
    color_bars();
    (void)Board_ConsoleWrite("Touch to paint; c=restore bars; p=polling, i=interrupt, d=DMA.\r\n"
                             "Mode changes redraw bars; initial calibration estimates.\r\n");
    uint32_t last_report = HAL_GetTick() - 200U;
    for (;;)
    {
        XPT2046_Point raw;
        require_ok("Touch read=", XPT2046_ReadRaw(&raw));
        if (raw.pressed != 0U)
        {
            uint16_t x, y;
            XPT2046_ToScreen(&raw, &x, &y);
            uint16_t left = x >= 2U ? (uint16_t)(x - 2U) : 0U;
            uint16_t top = y >= 2U ? (uint16_t)(y - 2U) : 0U;
            require_ok("LCD touch dot=", ILI9341_FillRect(left, top, 5U, 5U, ILI9341_WHITE));
            uint32_t now = HAL_GetTick();
            if ((uint32_t)(now - last_report) >= 200U)
            {
                last_report = now;
                (void)Board_ConsoleWrite("raw x=");
                (void)Board_ConsoleWriteU32(raw.x);
                (void)Board_ConsoleWrite(" y=");
                (void)Board_ConsoleWriteU32(raw.y);
                (void)Board_ConsoleWrite(" -> pixel x=");
                (void)Board_ConsoleWriteU32(x);
                (void)Board_ConsoleWrite(" y=");
                (void)Board_ConsoleWriteU32(y);
                (void)Board_ConsoleWrite("\r\n");
            }
        }
        uint8_t byte;
        if (HAL_UART_Receive(&huart0, &byte, 1U, 0U) == HAL_OK)
        {
            if (byte == 'p' || byte == 'i' || byte == 'd')
            {
                Display_SPI_Mode mode = byte == 'p'   ? DISPLAY_SPI_POLLING
                                        : byte == 'i' ? DISPLAY_SPI_INTERRUPT
                                                      : DISPLAY_SPI_DMA;
                require_ok("SPI mode=", Display_SPI_SetMode(mode));
                (void)Board_ConsoleWrite(byte == 'p'   ? "Mode=polling\r\n"
                                         : byte == 'i' ? "Mode=interrupt\r\n"
                                                       : "Mode=DMA\r\n");
                color_bars();
                (void)Board_ConsoleWrite("Color bars complete; touch to paint.\r\n");
            }
            else if (byte == 'c' || byte == 'C')
            {
                color_bars();
            }
        }
        HAL_Delay(20U);
    }
}
