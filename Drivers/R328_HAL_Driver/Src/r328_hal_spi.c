#include "r328_hal_bus_private.h"
#define SPI_XCH (1UL << 31)
#define SPI_TC (1U << 12)
#define SPI_FIFO_ERRORS (0xFU << 8)
#define SPI_FIFO_RESET ((1UL << 31) | (1UL << 15))

static int valid_instance(const SPI_TypeDef *instance)
{
    return instance == SPI0 || instance == SPI1;
}
static HAL_StatusTypeDef finish(SPI_HandleTypeDef *handle, HAL_StatusTypeDef status)
{
    if (status != HAL_OK)
    {
        /* CCU reset aborts XCH too; writing zero to XCH cannot abort it. */
        (void)HAL_RCC_SPI_ForceReset(handle->Instance);
        handle->State = HAL_SPI_STATE_RESET;
    }
    else
    {
        BUS_WRITE(handle->Instance->TCR, BUS_READ(handle->Instance->TCR) | (1U << 7));
        handle->State = HAL_SPI_STATE_READY;
    }
    HAL_Unlock(&handle->Lock);
    return status;
}
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *handle)
{
    if (handle == NULL || !valid_instance(handle->Instance))
    {
        return HAL_ERROR;
    }
    const SPI_InitTypeDef *init = &handle->Init;
    if (init->Mode != SPI_MODE_MASTER || init->DataSize != SPI_DATASIZE_8BIT ||
        init->BaudRateHz == 0U || init->BaudRateHz > 12000000U || init->ChipSelect > 3U ||
        (init->CLKPolarity != SPI_POLARITY_LOW && init->CLKPolarity != SPI_POLARITY_HIGH) ||
        (init->CLKPhase != SPI_PHASE_1EDGE && init->CLKPhase != SPI_PHASE_2EDGE) ||
        (init->FirstBit != SPI_FIRSTBIT_MSB && init->FirstBit != SPI_FIRSTBIT_LSB) ||
        (init->NSS != SPI_NSS_SOFT && init->NSS != SPI_NSS_HARD_OUTPUT))
    {
        return HAL_ERROR;
    }
    /* Pick the fastest exact rational clock <= requested, from both dividers.
     * Do not compare truncated Hz: that could overclock a slow peripheral. */
    uint32_t best_divisor = UINT32_MAX, clock = 0U;
    for (uint32_t n = 0U; n < 256U; ++n)
    {
        uint32_t divisor = 2U * (n + 1U);
        if ((uint64_t)init->BaudRateHz * divisor >= R328_OSC24M_HZ && divisor < best_divisor)
        {
            best_divisor = divisor;
            clock = (1U << 12) | n;
        }
    }
    for (uint32_t n = 1U; n < 16U; ++n)
    {
        uint32_t divisor = 1UL << n;
        if ((uint64_t)init->BaudRateHz * divisor >= R328_OSC24M_HZ && divisor < best_divisor)
        {
            best_divisor = divisor;
            clock = n << 8;
        }
    }
    if (best_divisor == UINT32_MAX)
    {
        return HAL_ERROR;
    }
    if (HAL_TryLock(&handle->Lock) != HAL_OK)
    {
        return HAL_BUSY;
    }
    if (handle->State == HAL_SPI_STATE_RESET)
    {
        HAL_SPI_MspInit(handle);
    }
    (void)HAL_RCC_SPI_EnableClock(handle->Instance);
    (void)HAL_RCC_SPI_ForceReset(handle->Instance);
    BUS_WRITE(handle->Instance->GCR, 0x83U); /* pause on RX full, master, enable */
    BUS_WRITE(handle->Instance->TCR, init->CLKPolarity | init->CLKPhase | init->FirstBit |
                                         (1U << 13) | (1U << 9) | (1U << 7) | (1U << 6) |
                                         (1U << 2) | (init->ChipSelect << 4));
    BUS_WRITE(handle->Instance->CCR, clock);
    BUS_WRITE(handle->Instance->IER, 0U);
    BUS_WRITE(handle->Instance->WCR, 0U);
    BUS_WRITE(handle->Instance->ISR, 0x3F77U); /* direct W1C */
    handle->ErrorCode = HAL_SPI_ERROR_NONE;
    handle->ActualBaudRateHz = R328_OSC24M_HZ / best_divisor;
    return finish(handle, HAL_OK);
}
HAL_StatusTypeDef HAL_SPI_DeInit(SPI_HandleTypeDef *handle)
{
    if (handle == NULL || !valid_instance(handle->Instance))
    {
        return HAL_ERROR;
    }
    if (HAL_TryLock(&handle->Lock) != HAL_OK)
    {
        return HAL_BUSY;
    }
    (void)HAL_RCC_SPI_ForceReset(handle->Instance);
    HAL_SPI_MspDeInit(handle);
    handle->State = HAL_SPI_STATE_RESET;
    HAL_Unlock(&handle->Lock);
    return HAL_OK;
}
static HAL_StatusTypeDef transfer(SPI_HandleTypeDef *handle, const uint8_t *tx, uint8_t *rx,
                                  uint32_t size, uint32_t timeout)
{
    if (handle == NULL || !valid_instance(handle->Instance) ||
        handle->State == HAL_SPI_STATE_RESET || (tx == NULL && rx == NULL && size != 0U))
    {
        return HAL_ERROR;
    }
    if (HAL_TryLock(&handle->Lock) != HAL_OK)
    {
        return HAL_BUSY;
    }
    handle->State = HAL_SPI_STATE_BUSY;
    handle->ErrorCode = HAL_SPI_ERROR_NONE;
    SPI_TypeDef *spi = handle->Instance;
    uint32_t start = HAL_GetTick();
    if (size != 0U && handle->Init.NSS == SPI_NSS_HARD_OUTPUT)
    {
        BUS_WRITE(spi->TCR, BUS_READ(spi->TCR) & ~(1U << 7));
    }
    /* Preload complete bursts so polling latency cannot underrun the FIFO.
     * SS stays asserted across bursts, with clock gaps every 64 bytes. */
    for (uint32_t offset = 0U; offset < size;)
    {
        uint32_t count = size - offset > 64U ? 64U : size - offset;
        BUS_WRITE(spi->FCR, SPI_FIFO_RESET | (32U << 16) | 1U);
        if (bus_wait_clear(&spi->FCR, SPI_FIFO_RESET) != HAL_OK)
        {
            handle->ErrorCode = HAL_SPI_ERROR_TIMEOUT;
            return finish(handle, HAL_TIMEOUT);
        }
        BUS_WRITE(spi->ISR, 0x3F77U);
        uint32_t tcr = BUS_READ(spi->TCR) & ~(1U << 8);
        if (rx == NULL)
        {
            tcr |= 1U << 8;
        } /* discard TX-only receive bytes */
        BUS_WRITE(spi->TCR, tcr);
        BUS_WRITE(spi->MBC, count);
        BUS_WRITE(spi->MTC, tx != NULL ? count : 0U);
        BUS_WRITE(spi->BCC, tx != NULL ? count : 0U);
        if (tx != NULL)
        {
            for (uint32_t i = 0U; i < count; ++i)
            {
                BUS_WRITE8(spi->TXD, tx[offset + i]);
            }
        }
        BUS_WRITE(spi->TCR, tcr | SPI_XCH);
        uint32_t received = 0U;
        for (;;)
        {
            uint32_t status = BUS_READ(spi->ISR);
            if ((status & SPI_FIFO_ERRORS) != 0U)
            {
                handle->ErrorCode = HAL_SPI_ERROR_FIFO;
                return finish(handle, HAL_ERROR);
            }
            while (rx != NULL && received < count && (BUS_READ(spi->FSR) & 0xFFU) != 0U)
            {
                rx[offset + received++] = BUS_READ8(spi->RXD);
            }
            if ((status & SPI_TC) != 0U && (BUS_READ(spi->TCR) & SPI_XCH) == 0U &&
                (rx == NULL || received == count))
            {
                break;
            }
            if (bus_expired(start, timeout))
            {
                handle->ErrorCode = HAL_SPI_ERROR_TIMEOUT;
                return finish(handle, HAL_TIMEOUT);
            }
        }
        BUS_WRITE(spi->ISR, SPI_TC);
        offset += count;
    }
    return finish(handle, HAL_OK);
}
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *handle, const uint8_t *data, uint32_t size,
                                   uint32_t timeout)
{
    if (data == NULL && size != 0U)
    {
        return HAL_ERROR;
    }
    return transfer(handle, data, NULL, size, timeout);
}
HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *handle, uint8_t *data, uint32_t size,
                                  uint32_t timeout)
{
    if (data == NULL && size != 0U)
    {
        return HAL_ERROR;
    }
    return transfer(handle, NULL, data, size, timeout);
}
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *handle, const uint8_t *tx, uint8_t *rx,
                                          uint32_t size, uint32_t timeout)
{
    if ((tx == NULL || rx == NULL) && size != 0U)
    {
        return HAL_ERROR;
    }
    return transfer(handle, tx, rx, size, timeout);
}
__weak void HAL_SPI_MspInit(SPI_HandleTypeDef *handle)
{
    UNUSED(handle);
}
__weak void HAL_SPI_MspDeInit(SPI_HandleTypeDef *handle)
{
    UNUSED(handle);
}
