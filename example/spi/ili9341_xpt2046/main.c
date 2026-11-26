#include "main.h"
#include "ili9341.h"
#include "xpt2046.h"
#include "display_bus.h"

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

static void display_require_ok(const char *label, HAL_StatusTypeDef status)
{
    if (status != HAL_OK)
    {
        HAL_UART_PrintValue(&huart0, label, status);
        HAL_UART_PrintValue(&huart0, "SPI ErrorCode=", hspi1.ErrorCode);
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
        display_require_ok(
            "LCD bars=", ILI9341_FillRect((uint16_t)(i * 30U), 0U, 30U, ILI9341_HEIGHT, colors[i]));
    }
    display_require_ok("LCD border=", ILI9341_FillRect(0U, 0U, ILI9341_WIDTH, 2U, ILI9341_WHITE));
    display_require_ok("LCD border=",
                       ILI9341_FillRect(0U, ILI9341_HEIGHT - 2U, ILI9341_WIDTH, 2U, ILI9341_WHITE));
    display_require_ok("LCD border=", ILI9341_FillRect(0U, 0U, 2U, ILI9341_HEIGHT, ILI9341_WHITE));
    display_require_ok("LCD border=",
                       ILI9341_FillRect(ILI9341_WIDTH - 2U, 0U, 2U, ILI9341_HEIGHT, ILI9341_WHITE));
}
int main(void)
{
    app_init("ILI9341 + XPT2046 / shared SPI1 / per-device clock / mode 0");
    display_require_ok("SPI/DMA init=", Display_SPI_Init());
    HAL_StatusTypeDef status = ILI9341_Init();
    HAL_UART_PrintValue(&huart0, "ILI9341 init=", status);
    display_require_ok("ILI9341 failed=", status);
    HAL_UART_PrintValue(&huart0, "LCD actual SPI Hz=", hspi1.ActualBaudRateHz);
    status = XPT2046_Init();
    HAL_UART_PrintValue(&huart0, "XPT2046 init=", status);
    display_require_ok("XPT2046 failed=", status);
    HAL_UART_PrintValue(&huart0, "Touch actual SPI Hz=", hspi1.ActualBaudRateHz);

    static const uint16_t fills[] = {ILI9341_RED, ILI9341_GREEN, ILI9341_BLUE};
    for (uint32_t i = 0U; i < sizeof(fills) / sizeof(fills[0]); ++i)
    {
        display_require_ok("LCD fill=", ILI9341_Fill(fills[i]));
        HAL_Delay(700U);
    }
    color_bars();
    (void)HAL_UART_PrintString(&huart0,
                               "Touch to paint; c=restore bars; p=polling, i=interrupt, d=DMA.\r\n"
                               "Mode changes redraw bars; initial calibration estimates.\r\n");
    uint32_t last_report = HAL_GetTick() - 200U;
    for (;;)
    {
        XPT2046_Point raw;
        display_require_ok("Touch read=", XPT2046_ReadRaw(&raw));
        if (raw.pressed != 0U)
        {
            uint16_t x, y;
            XPT2046_ToScreen(&raw, &x, &y);
            uint16_t left = x >= 2U ? (uint16_t)(x - 2U) : 0U;
            uint16_t top = y >= 2U ? (uint16_t)(y - 2U) : 0U;
            display_require_ok("LCD touch dot=",
                               ILI9341_FillRect(left, top, 5U, 5U, ILI9341_WHITE));
            uint32_t now = HAL_GetTick();
            if ((uint32_t)(now - last_report) >= 200U)
            {
                last_report = now;
                (void)HAL_UART_PrintString(&huart0, "raw x=");
                (void)HAL_UART_PrintU32(&huart0, raw.x);
                (void)HAL_UART_PrintString(&huart0, " y=");
                (void)HAL_UART_PrintU32(&huart0, raw.y);
                (void)HAL_UART_PrintString(&huart0, " -> pixel x=");
                (void)HAL_UART_PrintU32(&huart0, x);
                (void)HAL_UART_PrintString(&huart0, " y=");
                (void)HAL_UART_PrintU32(&huart0, y);
                (void)HAL_UART_PrintString(&huart0, "\r\n");
            }
        }
        uint8_t byte;
        if (HAL_UART_GetRxFifoLevel(&huart0) != 0U &&
            HAL_UART_Receive(&huart0, &byte, 1U) == HAL_OK)
        {
            if (byte == 'p' || byte == 'i' || byte == 'd')
            {
                Display_SPI_Mode mode = byte == 'p'   ? DISPLAY_SPI_POLLING
                                        : byte == 'i' ? DISPLAY_SPI_INTERRUPT
                                                      : DISPLAY_SPI_DMA;
                display_require_ok("SPI mode=", Display_SPI_SetMode(mode));
                (void)HAL_UART_PrintString(&huart0, byte == 'p'   ? "Mode=polling\r\n"
                                                    : byte == 'i' ? "Mode=interrupt\r\n"
                                                                  : "Mode=DMA\r\n");
                color_bars();
                (void)HAL_UART_PrintString(&huart0, "Color bars complete; touch to paint.\r\n");
            }
            else if (byte == 'c' || byte == 'C')
            {
                color_bars();
            }
        }
        HAL_Delay(20U);
    }
}
