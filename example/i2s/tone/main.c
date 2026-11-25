#include "example.h"

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
        Example_Report("I2S reinit=", status);
        if (status != HAL_OK)
        {
            return;
        }
    }
    prepare_tone(channel);
    (void)Board_ConsoleWrite(channel == 'l'   ? "Left only: 1 kHz, 200 ms.\r\n"
                             : channel == 'r' ? "Right only: 500 Hz, 200 ms.\r\n"
                                              : "Stereo: L=1 kHz, R=500 Hz, 200 ms.\r\n");
    uint32_t start = HAL_GetTick();
    /* One transfer: no stop/start gaps within the tone. */
    HAL_StatusTypeDef status = HAL_I2S_Transmit(&hi2s0, samples, TONE_FRAMES * 2U, 1000U);
    uint32_t elapsed = HAL_GetTick() - start;
    Example_Report("I2S tone=", status);
    Example_PrintValue("elapsed ms=", elapsed);
    if (status != HAL_OK)
    {
        Example_PrintValue("ErrorCode=", hi2s0.ErrorCode);
        Example_PrintValue("TX samples remaining=", hi2s0.TxXferCount);
        (void)Board_ConsoleWrite("Stopped. Press p/l/r to reinitialize and retry.\r\n");
        return;
    }
    (void)Board_ConsoleWrite("200 ms sine complete.\r\n");
}

int main(void)
{
    Example_Init("I2S0 / 200 ms PCM16 sine");
    Example_RequireOK("I2S init=", Board_I2S0_Init(48000U, I2S_DATAFORMAT_16B));
    Example_PrintValue("CPU Hz=", SystemCoreClock);
    (void)Board_ConsoleWrite("CLKD=0x");
    Example_PrintHex(I2S0->CLKD);
    (void)Board_ConsoleWrite("\r\n");

    /* Optional external amplifier enable; PB5 must remain GPIO, not I2S MCLK. */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);
    GPIO_InitTypeDef gpio_init = {.Pin = GPIO_PIN_5,
                                  .Mode = GPIO_MODE_OUTPUT_PP,
                                  .Pull = GPIO_NOPULL,
                                  .Drive = GPIO_DRIVE_LEVEL0};
    Example_RequireOK("Amp GPIO init=", HAL_GPIO_Init(GPIOB, &gpio_init));
    HAL_Delay(1U);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);
    HAL_Delay(10U);
    (void)Board_ConsoleWrite("p=stereo, l=left only, r=right only; each plays 200 ms.\r\n");
    for (;;)
    {
        uint8_t byte;
        if (HAL_UART_Receive(&huart0, &byte, 1U, 0U) == HAL_OK &&
            (byte == 'p' || byte == 'l' || byte == 'r'))
        {
            play(byte);
        }
    }
}
