#include "example.h"

#define TONE_FRAMES 480U
static uint32_t samples[TONE_FRAMES * 2U];

static void prepare_tone(void)
{
    for (uint32_t frame = 0U; frame < TONE_FRAMES; ++frame)
    {
        /* 48 kHz / 48 = 1 kHz left; / 96 = 500 Hz right, about -24 dBFS. */
        uint32_t left_phase = frame % 48U, right_phase = frame % 96U;
        int32_t left =
            (int32_t)((left_phase < 24U ? left_phase : 48U - left_phase) * 4096U / 24U) - 2048;
        int32_t right =
            (int32_t)((right_phase < 48U ? right_phase : 96U - right_phase) * 4096U / 48U) - 2048;
        samples[frame * 2U] = (uint16_t)(int16_t)left;
        samples[frame * 2U + 1U] = (uint16_t)(int16_t)right;
    }
}

static void play(void)
{
    /* Finite 10 ms blocks have stop/start gaps; this is not continuous playback. */
    for (uint32_t block = 0U; block < 20U; ++block)
    {
        Example_RequireOK("I2S tone=", HAL_I2S_Transmit(&hi2s0, samples, TONE_FRAMES * 2U, 100U));
    }
    (void)Board_ConsoleWrite("20 finite tone blocks complete.\r\n");
}

int main(void)
{
    Example_Init("I2S0 / stereo PCM16 triangle bursts");
    Example_RequireOK("I2S init=", Board_I2S0_Init(48000U, I2S_DATAFORMAT_16B));
    prepare_tone();
    (void)Board_ConsoleWrite("L=1 kHz, R=500 Hz; press p to send 20 finite blocks.\r\n");
    for (;;)
    {
        uint8_t byte;
        if (HAL_UART_Receive(&huart0, &byte, 1U, 0U) == HAL_OK && byte == 'p')
        {
            play();
        }
    }
}
