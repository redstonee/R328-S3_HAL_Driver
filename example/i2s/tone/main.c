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

#define TONE_FRAMES 9600U
#define FADE_FRAMES 240U

/* 200 ms at 48 kHz. The 75 KiB buffer needs the full SRAM or DDR profile. */
static uint32_t samples[TONE_FRAMES * 2U];
static const int16_t sine[96] = {
    0,     134,   267,   400,   530,   658,   784,   906,   1024,  1138,  1247,  1350,
    1448,  1540,  1625,  1703,  1774,  1837,  1892,  1939,  1978,  2009,  2030,  2044,
    2048,  2044,  2030,  2009,  1978,  1939,  1892,  1837,  1774,  1703,  1625,  1540,
    1448,  1350,  1247,  1138,  1024,  906,   784,   658,   530,   400,   267,   134,
    0,     -134,  -267,  -400,  -530,  -658,  -784,  -906,  -1024, -1138, -1247, -1350,
    -1448, -1540, -1625, -1703, -1774, -1837, -1892, -1939, -1978, -2009, -2030, -2044,
    -2048, -2044, -2030, -2009, -1978, -1939, -1892, -1837, -1774, -1703, -1625, -1540,
    -1448, -1350, -1247, -1138, -1024, -906,  -784,  -658,  -530,  -400,  -267,  -134,
};

static void prepare_tone(uint8_t channel)
{
    for (uint32_t frame = 0U; frame < TONE_FRAMES; ++frame)
    {
        /* 5 ms fades start and end at zero to reduce clicks. Peak is -24 dBFS. */
        uint32_t gain = frame < FADE_FRAMES ? frame : FADE_FRAMES;
        uint32_t tail = TONE_FRAMES - 1U - frame;
        if (tail < gain)
        {
            gain = tail;
        }
        int32_t left = channel == 'r' ? 0 : sine[(frame * 2U) % 96U];
        int32_t right = channel == 'l' ? 0 : sine[frame % 96U];
        left = left * (int32_t)gain / (int32_t)FADE_FRAMES;
        right = right * (int32_t)gain / (int32_t)FADE_FRAMES;
        samples[frame * 2U] = (uint16_t)(int16_t)left;
        samples[frame * 2U + 1U] = (uint16_t)(int16_t)right;
    }
}

static void play(uint8_t channel)
{
    if (hi2s0.State == HAL_I2S_STATE_RESET)
    {
        HAL_StatusTypeDef status = HAL_I2S_Init(&hi2s0);
        HAL_UART_PrintValue(&huart0, "I2S reinit=", status);
        if (status != HAL_OK)
        {
            return;
        }
    }
    prepare_tone(channel);
    (void)HAL_UART_PrintString(&huart0, channel == 'l' ? "Left only: 1 kHz, 200 ms.\r\n"
                                        : channel == 'r'
                                            ? "Right only: 500 Hz, 200 ms.\r\n"
                                            : "Stereo: L=1 kHz, R=500 Hz, 200 ms.\r\n");
    uint32_t start = HAL_GetTick();
    /* One transfer: no stop/start gaps within the tone. */
    HAL_StatusTypeDef status = HAL_I2S_Transmit(&hi2s0, samples, TONE_FRAMES * 2U);
    uint32_t elapsed = HAL_GetTick() - start;
    HAL_UART_PrintValue(&huart0, "I2S tone=", status);
    HAL_UART_PrintValue(&huart0, "elapsed ms=", elapsed);
    if (status != HAL_OK)
    {
        HAL_UART_PrintValue(&huart0, "ErrorCode=", hi2s0.ErrorCode);
        HAL_UART_PrintValue(&huart0, "TX samples remaining=", hi2s0.TxXferCount);
        (void)HAL_UART_PrintString(&huart0, "Stopped. Press p/l/r to reinitialize and retry.\r\n");
        return;
    }
    (void)HAL_UART_PrintString(&huart0, "200 ms sine complete.\r\n");
}

int main(void)
{
    app_init("I2S0 / 200 ms PCM16 sine");
    HAL_I2S_SetPollingTimeout(1000U);
    require_ok("I2S init=", i2s_init(48000U, I2S_DATAFORMAT_16B));
    HAL_UART_PrintValue(&huart0, "CPU Hz=", SystemCoreClock);
    (void)HAL_UART_PrintString(&huart0, "CLKD=0x");
    HAL_UART_PrintHex(&huart0, I2S0->CLKD);
    (void)HAL_UART_PrintString(&huart0, "\r\n");

    /* Optional external amplifier enable; PB5 must remain GPIO, not I2S MCLK. */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);
    GPIO_InitTypeDef gpio_init = {.Pin = GPIO_PIN_5,
                                  .Mode = GPIO_MODE_OUTPUT_PP,
                                  .Pull = GPIO_NOPULL,
                                  .Drive = GPIO_DRIVE_LEVEL0};
    require_ok("Amp GPIO init=", HAL_GPIO_Init(GPIOB, &gpio_init));
    HAL_Delay(1U);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);
    HAL_Delay(10U);
    (void)HAL_UART_PrintString(&huart0,
                               "p=stereo, l=left only, r=right only; each plays 200 ms.\r\n");
    for (;;)
    {
        uint8_t byte;
        if (HAL_UART_GetRxFifoLevel(&huart0) != 0U &&
            HAL_UART_Receive(&huart0, &byte, 1U) == HAL_OK &&
            (byte == 'p' || byte == 'l' || byte == 'r'))
        {
            play(byte);
        }
    }
}
