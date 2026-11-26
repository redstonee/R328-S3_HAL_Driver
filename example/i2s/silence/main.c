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

static void idle(void) __attribute__((noreturn));
static void idle(void)
{
    /* Polling RX needs the CPU; the architecture counter does not wake WFI. */
    for (;;)
    {
        uart_poll();
    }
}

static I2S_HandleTypeDef hi2s0;
static HAL_StatusTypeDef i2s_init(uint32_t sample_rate, uint32_t sample_bits)
{
    if ((sample_bits != 16U && sample_bits != 24U && sample_bits != 32U) || sample_rate < 8000U ||
        sample_rate > 192000U)
    {
        return HAL_ERROR;
    }
    uint32_t family;
    if (sample_rate == 11025U || sample_rate == 22050U || sample_rate == 44100U ||
        sample_rate == 88200U || sample_rate == 176400U)
    {
        family = RCC_AUDIO_FAMILY_44K1;
    }
    else if (sample_rate == 8000U || sample_rate == 12000U || sample_rate == 16000U ||
             sample_rate == 24000U || sample_rate == 32000U || sample_rate == 48000U ||
             sample_rate == 96000U || sample_rate == 192000U)
    {
        family = RCC_AUDIO_FAMILY_48K;
    }
    else
    {
        return HAL_ERROR;
    }
    HAL_StatusTypeDef status = HAL_RCC_AudioPLL_Config(family);
    if (status != HAL_OK)
    {
        return status;
    }
    GPIO_InitTypeDef pins = {.Pin = GPIO_PIN_2 | GPIO_PIN_3,
                             .Mode = GPIO_MODE_AF_PP,
                             .Pull = GPIO_NOPULL,
                             .Drive = GPIO_DRIVE_LEVEL1,
                             .Alternate = 5U};
    status = HAL_GPIO_Init(GPIOB, &pins);
    if (status != HAL_OK)
    {
        return status;
    }
    pins.Pin = GPIO_PIN_4;
    pins.Alternate = 4U;
    status = HAL_GPIO_Init(GPIOB, &pins);
    if (status != HAL_OK)
    {
        return status;
    }
    hi2s0.Instance = I2S0;
    hi2s0.Init = (I2S_InitTypeDef){.Mode = I2S_MODE_MASTER_TX,
                                   .Standard = I2S_STANDARD_PHILIPS,
                                   .DataFormat = sample_bits,
                                   .SlotWidth = 32U,
                                   .AudioFreq = sample_rate,
                                   .MCLKOutput = I2S_MCLKOUTPUT_DISABLE,
                                   .MCLKMultiple = 256U};
    return HAL_I2S_Init(&hi2s0);
}

static const uint32_t silence[256]; /* 128 frames, L/R PCM16 in 32-bit slots. */
int main(void)
{
    app_init("I2S0 / finite silence block");
    HAL_I2S_SetPollingTimeout(100U);
    HAL_StatusTypeDef status = i2s_init(48000U, I2S_DATAFORMAT_16B);
    HAL_UART_PrintValue(&huart0, "I2S0 init=", status);
    if (status != HAL_OK)
    {
        Error_Handler();
    }
    status = HAL_I2S_Transmit(&hi2s0, silence, 256U);
    HAL_UART_PrintValue(&huart0, "I2S0 silence=", status);
    if (status == HAL_OK)
    {
        (void)HAL_UART_PrintString(&huart0, "Inspect LRCK/BCLK/DOUT; finite block complete.\r\n");
    }
    else
    {
        HAL_UART_PrintValue(&huart0, "ErrorCode=", hi2s0.ErrorCode);
    }
    (void)HAL_UART_PrintString(&huart0, "UART echo active.\r\n");
    idle();
}
