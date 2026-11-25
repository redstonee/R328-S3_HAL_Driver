#include "r328_hal_bus_private.h"

static int spi_index(SPI_TypeDef *instance)
{
    return instance == SPI0 ? 0 : instance == SPI1 ? 1 : -1;
}
static int twi_index(I2C_TypeDef *instance)
{
    return instance == I2C0 ? 0 : instance == I2C1 ? 1 : -1;
}
static int i2s_index(I2S_TypeDef *instance)
{
    return instance == I2S0 ? 0 : instance == I2S1 ? 1 : instance == I2S2 ? 2 : -1;
}
static HAL_StatusTypeDef gate_reset(volatile uint32_t *bgr, int index, int reset)
{
    if (index < 0)
    {
        return HAL_ERROR;
    }
    uint32_t irq = R328_IRQ_Save();
    uint32_t value = BUS_READ(*bgr) | (1UL << (uint32_t)index);
    BUS_WRITE(*bgr, value);
    R328_DSB();
    uint32_t mask = 1UL << (16U + (uint32_t)index);
    if (reset)
    {
        BUS_WRITE(*bgr, value & ~mask);
        R328_DSB();
    }
    BUS_WRITE(*bgr, value | mask);
    R328_DSB();
    R328_IRQ_Restore(irq);
    return HAL_OK;
}
HAL_StatusTypeDef HAL_RCC_LEDC_EnableClock(LEDC_TypeDef *instance)
{
    if (instance != LEDC)
    {
        return HAL_ERROR;
    }
    BUS_WRITE(RCC_LEDC_CLK, 1UL << 31); /* OSC24M, /1: timing cycles are fixed. */
    R328_DSB();
    return gate_reset(&RCC_LEDC_BGR, 0, 0);
}
HAL_StatusTypeDef HAL_RCC_LEDC_ForceReset(LEDC_TypeDef *instance)
{
    return gate_reset(&RCC_LEDC_BGR, instance == LEDC ? 0 : -1, 1);
}
HAL_StatusTypeDef HAL_RCC_SPI_EnableClock(SPI_TypeDef *instance)
{
    int index = spi_index(instance);
    if (index < 0)
    {
        return HAL_ERROR;
    }
    /* Fixed OSC24M module clock; CCR supplies the serial divider. */
    BUS_WRITE(RCC_SPI_CLK((uint32_t)index), 1UL << 31);
    R328_DSB();
    return gate_reset(&RCC_SPI_BGR, index, 0);
}
HAL_StatusTypeDef HAL_RCC_SPI_ForceReset(SPI_TypeDef *instance)
{
    return gate_reset(&RCC_SPI_BGR, spi_index(instance), 1);
}
HAL_StatusTypeDef HAL_RCC_I2C_EnableClock(I2C_TypeDef *instance)
{
    return gate_reset(&RCC_TWI_BGR, twi_index(instance), 0);
}
HAL_StatusTypeDef HAL_RCC_I2C_ForceReset(I2C_TypeDef *instance)
{
    return gate_reset(&RCC_TWI_BGR, twi_index(instance), 1);
}

uint32_t HAL_RCC_GetAudioPLL4XFreq(void)
{
    uint32_t control = BUS_READ(RCC_PLL_AUDIO_CTRL);
    /* CCU names this parent PLL_AUDIO(4X), but the manual 5.1.4 recipes
     * supply 24.576/22.5792 MHz to I2S on R328. Using the un-post-divided
     * formula from 3.3.5.5 makes BCLK/LRCK twelve times too slow. Include
     * P=11 in the recognized recipe; it is not a don't-care field. */
    if ((control & 0xF93FFFFFU) != 0xB90B1701U && (control & 0xF93FFFFFU) != 0xB90B1501U)
    {
        return 0U;
    }
    uint32_t pattern = BUS_READ(RCC_PLL_AUDIO_PAT0);
    if ((control & 0xFFFFU) == 0x1701U && pattern == 0xC00126E9U)
    {
        return 24576000U;
    }
    if ((control & 0xFFFFU) == 0x1501U && pattern == 0xC001288DU)
    {
        return 22579200U;
    }
    return 0U;
}
HAL_StatusTypeDef HAL_RCC_AudioPLL_Config(uint32_t family)
{
    if (family != RCC_AUDIO_FAMILY_48K && family != RCC_AUDIO_FAMILY_44K1)
    {
        return HAL_ERROR;
    }
    uint32_t target = family == RCC_AUDIO_FAMILY_48K ? 24576000U : 22579200U;
    if (HAL_RCC_GetAudioPLL4XFreq() == target)
    {
        return HAL_OK;
    }
    /* These consumers share PLL_AUDIO; do not retune an active one. */
    const uint32_t offsets[] = {0xA10U, 0xA14U, 0xA18U, 0xA20U, 0xA40U, 0xA50U, 0xA54U};
    for (uint32_t i = 0U; i < sizeof(offsets) / sizeof(offsets[0]); ++i)
    {
        if ((BUS_READ(R328_CCU_REG(offsets[i])) & (1UL << 31)) != 0U)
        {
            return HAL_BUSY;
        }
    }
    BUS_WRITE(RCC_PLL_AUDIO_CTRL, BUS_READ(RCC_PLL_AUDIO_CTRL) & ~(1UL << 31));
    R328_DSB();
    BUS_WRITE(RCC_PLL_AUDIO_PAT0, family == RCC_AUDIO_FAMILY_48K ? 0xC00126E9U : 0xC001288DU);
    BUS_WRITE(RCC_PLL_AUDIO_CTRL, family == RCC_AUDIO_FAMILY_48K ? 0xA90B1701U : 0xA90B1501U);
    for (uint32_t count = 0U; count < HAL_REGISTER_POLL_LIMIT; ++count)
    {
        if ((BUS_READ(RCC_PLL_AUDIO_CTRL) & (1UL << 28)) != 0U)
        {
            return HAL_OK;
        }
    }
    BUS_WRITE(RCC_PLL_AUDIO_CTRL, BUS_READ(RCC_PLL_AUDIO_CTRL) & ~(1UL << 31));
    return HAL_TIMEOUT;
}
HAL_StatusTypeDef HAL_RCC_I2S_EnableClock(I2S_TypeDef *instance)
{
    int index = i2s_index(instance);
    if (index < 0 || HAL_RCC_GetAudioPLL4XFreq() == 0U)
    {
        return HAL_ERROR;
    }
    BUS_WRITE(RCC_I2S_CLK((uint32_t)index), 1UL << 31); /* PLL_AUDIO(4X), /1 */
    R328_DSB();
    return gate_reset(&RCC_I2S_BGR, index, 0);
}
HAL_StatusTypeDef HAL_RCC_I2S_ForceReset(I2S_TypeDef *instance)
{
    return gate_reset(&RCC_I2S_BGR, i2s_index(instance), 1);
}
uint32_t HAL_RCC_GetI2SClockFreq(I2S_TypeDef *instance)
{
    int index = i2s_index(instance);
    if (index < 0)
    {
        return 0U;
    }
    uint32_t cfg = BUS_READ(RCC_I2S_CLK((uint32_t)index));
    if ((cfg & (3UL << 24)) != 0U)
    {
        return 0U;
    }
    return HAL_RCC_GetAudioPLL4XFreq() / (1UL << ((cfg >> 8) & 3U));
}
