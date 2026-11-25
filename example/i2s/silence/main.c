#include "example.h"

static const uint32_t silence[256]; /* 128 frames, L/R PCM16 in 32-bit slots. */
int main(void)
{
    Example_Init("I2S0 / finite silence block");
    HAL_StatusTypeDef status = Board_I2S0_Init(48000U, I2S_DATAFORMAT_16B);
    Example_Report("I2S0 init=", status);
    if (status != HAL_OK)
    {
        Error_Handler();
    }
    status = HAL_I2S_Transmit(&hi2s0, silence, 256U, 100U);
    Example_Report("I2S0 silence=", status);
    (void)Board_ConsoleWrite(
        "Inspect LRCK/BCLK/DOUT; finite block complete. UART echo active.\r\n");
    Example_Idle();
}
