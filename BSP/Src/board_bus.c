#include "board.h"

SPI_HandleTypeDef hspi1;
I2C_HandleTypeDef hi2c1;
I2S_HandleTypeDef hi2s0;

HAL_StatusTypeDef Board_SPI1_Init(uint32_t baud_rate_hz)
{
    if (hspi1.State == HAL_SPI_STATE_BUSY)
    {
        return HAL_BUSY;
    }
    hspi1.Instance = SPI1;
    hspi1.Init = (SPI_InitTypeDef){.Mode = SPI_MODE_MASTER,
                                   .DataSize = SPI_DATASIZE_8BIT,
                                   .CLKPolarity = SPI_POLARITY_LOW,
                                   .CLKPhase = SPI_PHASE_1EDGE,
                                   .NSS = SPI_NSS_SOFT,
                                   .BaudRateHz = baud_rate_hz,
                                   .FirstBit = SPI_FIRSTBIT_MSB,
                                   .ChipSelect = 0U};
    return HAL_SPI_Init(&hspi1);
}
HAL_StatusTypeDef Board_SPI1_Select(Board_SPI_Device device)
{
    if (device != BOARD_SPI_NONE && device != BOARD_SPI_LCD && device != BOARD_SPI_TOUCH)
    {
        return HAL_ERROR;
    }
    if (hspi1.State == HAL_SPI_STATE_RESET && device != BOARD_SPI_NONE)
    {
        return HAL_ERROR;
    }
    if (HAL_TryLock(&hspi1.Lock) != HAL_OK)
    {
        return HAL_BUSY;
    }
    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_4 | GPIO_PIN_8, GPIO_PIN_SET);
    if (device != BOARD_SPI_NONE)
    {
        HAL_GPIO_WritePin(GPIOH, device == BOARD_SPI_LCD ? GPIO_PIN_4 : GPIO_PIN_8, GPIO_PIN_RESET);
    }
    HAL_Unlock(&hspi1.Lock);
    return HAL_OK;
}
HAL_StatusTypeDef Board_I2C1_Init(uint32_t clock_speed)
{
#if defined(R328_UART0_ROUTE_PB8_PB9)
    UNUSED(clock_speed);
    return HAL_ERROR; /* PB8/9 belong to the debug UART in that alternate build. */
#else
    hi2c1.Instance = I2C1;
    hi2c1.Init =
        (I2C_InitTypeDef){.ClockSpeed = clock_speed, .AddressingMode = I2C_ADDRESSINGMODE_7BIT};
    return HAL_I2C_Init(&hi2c1);
#endif
}
HAL_StatusTypeDef Board_I2S0_Init(uint32_t sample_rate, uint32_t sample_bits)
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
