/* Host-test peripheral setup; never linked into firmware. */
#include "bus_test_setup.h"

SPI_HandleTypeDef hspi1;
I2C_HandleTypeDef hi2c1;
I2S_HandleTypeDef hi2s0;

HAL_StatusTypeDef Test_SPI1_Init(uint32_t baud_rate_hz)
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
HAL_StatusTypeDef Test_SPI1_Select(Test_SPI_Device device)
{
    if (device != TEST_SPI_NONE && device != TEST_SPI_LCD && device != TEST_SPI_TOUCH)
    {
        return HAL_ERROR;
    }
    if (hspi1.State == HAL_SPI_STATE_RESET && device != TEST_SPI_NONE)
    {
        return HAL_ERROR;
    }
    if (HAL_TryLock(&hspi1.Lock) != HAL_OK)
    {
        return HAL_BUSY;
    }
    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_4 | GPIO_PIN_8, GPIO_PIN_SET);
    if (device != TEST_SPI_NONE)
    {
        HAL_GPIO_WritePin(GPIOH, device == TEST_SPI_LCD ? GPIO_PIN_4 : GPIO_PIN_8, GPIO_PIN_RESET);
    }
    HAL_Unlock(&hspi1.Lock);
    return HAL_OK;
}
HAL_StatusTypeDef Test_I2C1_Init(uint32_t clock_speed)
{
    hi2c1.Instance = I2C1;
    hi2c1.Init =
        (I2C_InitTypeDef){.ClockSpeed = clock_speed, .AddressingMode = I2C_ADDRESSINGMODE_7BIT};
    return HAL_I2C_Init(&hi2c1);
}
HAL_StatusTypeDef Test_I2S0_Init(uint32_t sample_rate, uint32_t sample_bits)
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

void HAL_SPI_MspInit(SPI_HandleTypeDef *handle)
{
    if (handle->Instance != SPI1)
    {
        return;
    }
    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_4 | GPIO_PIN_8, GPIO_PIN_SET);
    GPIO_InitTypeDef init = {.Pin = GPIO_PIN_4 | GPIO_PIN_8,
                             .Mode = GPIO_MODE_OUTPUT_PP,
                             .Pull = GPIO_PULLUP,
                             .Drive = GPIO_DRIVE_LEVEL1};
    if (HAL_GPIO_Init(GPIOH, &init) != HAL_OK)
    {
        Error_Handler();
    }
    init.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    init.Mode = GPIO_MODE_AF_PP;
    init.Alternate = 3U;
    init.Pull = GPIO_NOPULL;
    if (HAL_GPIO_Init(GPIOH, &init) != HAL_OK)
    {
        Error_Handler();
    }
}
void HAL_SPI_MspDeInit(SPI_HandleTypeDef *handle)
{
    if (handle->Instance != SPI1)
    {
        return;
    }
    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_4 | GPIO_PIN_8, GPIO_PIN_SET);
    (void)HAL_GPIO_DeInit(GPIOH, GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7);
}
void HAL_I2C_MspInit(I2C_HandleTypeDef *handle)
{
    if (handle->Instance != I2C1)
    {
        return;
    }
    GPIO_InitTypeDef init = {.Pin = GPIO_PIN_8 | GPIO_PIN_9,
                             .Mode = GPIO_MODE_AF_PP,
                             .Pull = GPIO_PULLUP,
                             .Drive = GPIO_DRIVE_LEVEL1,
                             .Alternate = 4U};
    /* TWI alternate function owns its open-drain output; GPIO has no OD bit. */
    if (HAL_GPIO_Init(GPIOB, &init) != HAL_OK)
    {
        Error_Handler();
    }
}
void HAL_I2C_MspDeInit(I2C_HandleTypeDef *handle)
{
    if (handle->Instance == I2C1)
    {
        (void)HAL_GPIO_DeInit(GPIOB, GPIO_PIN_8 | GPIO_PIN_9);
    }
}
void HAL_I2S_MspInit(I2S_HandleTypeDef *handle)
{
    if (handle->Instance != I2S0)
    {
        return;
    }
    /* Example routing provides TX only; DIN and MCLK require MSP changes. */
    GPIO_InitTypeDef init = {.Pin = GPIO_PIN_2 | GPIO_PIN_3,
                             .Mode = GPIO_MODE_AF_PP,
                             .Pull = GPIO_NOPULL,
                             .Drive = GPIO_DRIVE_LEVEL1,
                             .Alternate = 5U};
    if (HAL_GPIO_Init(GPIOB, &init) != HAL_OK)
    {
        Error_Handler();
    }
    init.Pin = GPIO_PIN_4;
    init.Alternate = 4U;
    if (HAL_GPIO_Init(GPIOB, &init) != HAL_OK)
    {
        Error_Handler();
    }
}
void HAL_I2S_MspDeInit(I2S_HandleTypeDef *handle)
{
    if (handle->Instance == I2S0)
    {
        (void)HAL_GPIO_DeInit(GPIOB, GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4);
    }
}
