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

#define WS2812_LED_COUNT 4U

static LEDC_HandleTypeDef hledc;

static HAL_StatusTypeDef ledc_init(uint32_t led_count)
{
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_2, GPIO_PIN_RESET);
    GPIO_InitTypeDef pin = {.Pin = GPIO_PIN_2,
                            .Mode = GPIO_MODE_AF_PP,
                            .Pull = GPIO_NOPULL,
                            .Drive = GPIO_DRIVE_LEVEL1,
                            .Alternate = 2U};
    HAL_StatusTypeDef status = HAL_GPIO_Init(GPIOE, &pin);
    if (status != HAL_OK)
    {
        return status;
    }
    hledc.Instance = LEDC;
    /* 24 MHz: each bit is 30 cycles (1.25 us), short pulse 7 cycles
     * (292 ns), long pulse 23 cycles (958 ns). XL-5050RGBC-WS2812B
     * specifies short 200..350 ns, long 550..1200 ns and reset >=80 us. */
    hledc.Init = (LEDC_InitTypeDef){.LEDCount = led_count,
                                    .T0H = 7U,
                                    .T0L = 23U,
                                    .T1H = 23U,
                                    .T1L = 7U,
                                    .ResetTimeUs = 300U,
                                    .WaitDataTimeUs = 50U};
    return HAL_LEDC_Init(&hledc);
}

static uint32_t pixels[WS2812_LED_COUNT];

static void Show(void)
{
    HAL_StatusTypeDef status = HAL_LEDC_Transmit(&hledc, pixels, WS2812_LED_COUNT);
    if (status != HAL_OK)
    {
        HAL_UART_PrintValue(&huart0, "LEDC error=", hledc.ErrorCode);
        require_ok("LEDC transmit=", status);
    }
}

static void Fill(uint8_t red, uint8_t green, uint8_t blue)
{
    for (uint32_t i = 0U; i < WS2812_LED_COUNT; ++i)
    {
        pixels[i] = HAL_LEDC_ColorRGB(red, green, blue);
    }
    Show();
}

int main(void)
{
    app_init("LEDC / WS2812, PE2 AF2, 4 pixels");
    HAL_LEDC_SetPollingTimeout(100U);
    require_ok("LEDC init=", ledc_init(WS2812_LED_COUNT));
    (void)HAL_UART_PrintString(&huart0, "pixel order: LED7 -> LED6 -> LED5 -> LED4\r\n");
    Fill(0U, 0U, 0U);
    for (;;)
    {
        (void)HAL_UART_PrintString(&huart0, "red / green / blue / white\r\n");
        Fill(16U, 0U, 0U);
        HAL_Delay(500U);
        Fill(0U, 16U, 0U);
        HAL_Delay(500U);
        Fill(0U, 0U, 16U);
        HAL_Delay(500U);
        Fill(8U, 8U, 8U);
        HAL_Delay(500U);

        (void)HAL_UART_PrintString(&huart0, "chase\r\n");
        for (uint32_t i = 0U; i < WS2812_LED_COUNT; ++i)
        {
            Fill(0U, 0U, 0U);
            pixels[i] = HAL_LEDC_ColorRGB(0U, 16U, 8U);
            Show();
            HAL_Delay(250U);
        }
        (void)HAL_UART_PrintString(&huart0, "blue breathing\r\n");
        for (uint32_t step = 0U; step <= 64U; ++step)
        {
            uint32_t brightness = step <= 32U ? step : 64U - step;
            Fill(0U, 0U, (uint8_t)brightness);
            HAL_Delay(20U);
        }
        HAL_UART_PrintValue(&huart0, "tick=", HAL_GetTick());
        uart_poll();
    }
}
