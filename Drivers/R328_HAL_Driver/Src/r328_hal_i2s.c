#include "r328_hal_bus_private.h"

static uint32_t polling_timeout = HAL_POLLING_DEFAULT_TIMEOUT_MS;
void HAL_I2S_SetPollingTimeout(uint32_t timeout)
{
    polling_timeout = timeout;
}
#define I2S_FIFO_RESET (3UL << 24)
#define I2S_ERRORS 0x66U
#define I2S_TX_FIFO_WORDS 128U
#define I2S_RX_FIFO_WORDS 64U

static void fill_tx(I2S_HandleTypeDef *handle, const uint32_t *tx, uint32_t size, uint32_t space)
{
    uint32_t count = handle->TxXferCount < space ? handle->TxXferCount : space;
    const uint32_t *next = tx + size - handle->TxXferCount;
    for (uint32_t i = 0U; i < count; ++i)
    {
        BUS_WRITE(handle->Instance->TXFIFO, next[i]);
    }
    handle->TxXferCount -= count;
}
static int valid_instance(I2S_TypeDef *instance)
{
    return instance == I2S0 || instance == I2S1 || instance == I2S2;
}
static uint32_t resolution(uint32_t bits)
{
    return bits == 16U ? 3U : bits == 24U ? 5U : bits == 32U ? 7U : 0U;
}
static uint32_t divider_code(uint32_t source, uint32_t target)
{
    const uint32_t divisors[] = {0U,  1U,  2U,  4U,  6U,  8U,   12U,  16U,
                                 24U, 32U, 48U, 64U, 96U, 128U, 176U, 192U};
    for (uint32_t i = 1U; i < 16U; ++i)
    {
        if ((uint64_t)target * divisors[i] == source)
        {
            return i;
        }
    }
    return 0U;
}
HAL_StatusTypeDef HAL_I2S_Init(I2S_HandleTypeDef *handle)
{
    if (handle == NULL || !valid_instance(handle->Instance))
    {
        return HAL_ERROR;
    }
    const I2S_InitTypeDef *init = &handle->Init;
    if (init->Mode > I2S_MODE_SLAVE_TX_RX || init->Standard > I2S_STANDARD_MSB ||
        resolution(init->DataFormat) == 0U || resolution(init->SlotWidth) == 0U ||
        init->DataFormat > init->SlotWidth || init->AudioFreq < 8000U ||
        init->AudioFreq > 192000U || init->MCLKOutput > 1U ||
        (init->MCLKOutput != 0U && init->MCLKMultiple != 256U && init->MCLKMultiple != 384U &&
         init->MCLKMultiple != 512U))
    {
        return HAL_ERROR;
    }
    uint32_t source = HAL_RCC_GetAudioPLL4XFreq();
    if (source == 0U)
    {
        return HAL_ERROR;
    } /* Board must explicitly configure shared PLL first. */
    uint32_t bclk = divider_code(source, init->AudioFreq * 2U * init->SlotWidth);
    uint32_t mclk =
        init->MCLKOutput == 0U ? 1U : divider_code(source, init->AudioFreq * init->MCLKMultiple);
    int master = init->Mode < I2S_MODE_SLAVE_TX;
    if ((master && bclk == 0U) || mclk == 0U)
    {
        return HAL_ERROR;
    }
    if (HAL_TryLock(&handle->Lock) != HAL_OK)
    {
        return HAL_BUSY;
    }
    if (handle->State == HAL_I2S_STATE_RESET)
    {
        HAL_I2S_MspInit(handle);
    }
    HAL_StatusTypeDef status = HAL_RCC_I2S_EnableClock(handle->Instance);
    if (status != HAL_OK)
    {
        HAL_Unlock(&handle->Lock);
        return status;
    }
    (void)HAL_RCC_I2S_ForceReset(handle->Instance);
    I2S_TypeDef *i2s = handle->Instance;
    BUS_WRITE(i2s->CTL, (master ? (3U << 17) : 0U) | (1U << 4)); /* I2S mode */
    BUS_WRITE(i2s->FMT0, ((init->SlotWidth - 1U) << 8) | (resolution(init->DataFormat) << 4) |
                             resolution(init->SlotWidth));
    BUS_WRITE(i2s->FMT1, 0x30U); /* MSB first, zero pad after sample. */
    BUS_WRITE(i2s->INTCTL, 0U);
    BUS_WRITE(i2s->FCTL,
              I2S_FIFO_RESET | (64U << 12) | (15U << 4) | 5U); /* TX LSB; RX sign extend. */
    status = bus_wait_clear(&i2s->FCTL, I2S_FIFO_RESET);
    if (status != HAL_OK)
    {
        handle->State = HAL_I2S_STATE_RESET;
        HAL_Unlock(&handle->Lock);
        return status;
    }
    BUS_WRITE(i2s->ISTA, I2S_ERRORS);
    BUS_WRITE(i2s->TXCNT, 0U);
    BUS_WRITE(i2s->RXCNT, 0U);
    BUS_WRITE(i2s->CHCFG, 0x11U); /* Two interleaved slots in each direction. */
    uint32_t offset = init->Standard == I2S_STANDARD_PHILIPS ? 1U << 20 : 0U;
    for (uint32_t i = 0U; i < 4U; ++i)
    {
        BUS_WRITE(i2s->TXCHSEL[i], i == 0U ? offset | (1U << 16) | 3U : 0U);
        BUS_WRITE(i2s->TXCHMAP[i][0], 0U);
        BUS_WRITE(i2s->TXCHMAP[i][1], i == 0U ? 0x10U : 0U);
        BUS_WRITE(i2s->RXCHMAP[i], i == 3U ? 0x100U : 0U); /* DIN0: left=0, right=1 */
    }
    BUS_WRITE(i2s->RXCHSEL, offset | (1U << 16));
    BUS_WRITE(i2s->CLKD, (init->MCLKOutput << 8) | ((bclk == 0U ? 1U : bclk) << 4) | mclk);
    handle->ErrorCode = HAL_I2S_ERROR_NONE;
    handle->State = HAL_I2S_STATE_READY;
    HAL_Unlock(&handle->Lock);
    return HAL_OK;
}
HAL_StatusTypeDef HAL_I2S_DeInit(I2S_HandleTypeDef *handle)
{
    if (handle == NULL || !valid_instance(handle->Instance))
    {
        return HAL_ERROR;
    }
    if (HAL_TryLock(&handle->Lock) != HAL_OK)
    {
        return HAL_BUSY;
    }
    (void)HAL_RCC_I2S_ForceReset(handle->Instance);
    uint32_t index = handle->Instance == I2S0 ? 0U : handle->Instance == I2S1 ? 1U : 2U;
    BUS_WRITE(RCC_I2S_CLK(index), 0U); /* Leave shared PLL and other users alone. */
    HAL_I2S_MspDeInit(handle);
    handle->State = HAL_I2S_STATE_RESET;
    HAL_Unlock(&handle->Lock);
    return HAL_OK;
}
static HAL_StatusTypeDef transfer(I2S_HandleTypeDef *handle, const uint32_t *tx, uint32_t *rx,
                                  uint32_t size, uint32_t timeout)
{
    if (handle == NULL || !valid_instance(handle->Instance) ||
        handle->State == HAL_I2S_STATE_RESET || (size & 1U) != 0U ||
        (size != 0U && tx == NULL && rx == NULL))
    {
        return HAL_ERROR;
    }
    uint32_t mode = handle->Init.Mode % 3U;
    if ((tx != NULL && mode == 1U) || (rx != NULL && mode == 0U))
    {
        return HAL_ERROR;
    }
    if (HAL_TryLock(&handle->Lock) != HAL_OK)
    {
        return HAL_BUSY;
    }
    handle->State = HAL_I2S_STATE_BUSY;
    handle->ErrorCode = HAL_I2S_ERROR_NONE;
    handle->TxXferCount = tx != NULL ? size : 0U;
    handle->RxXferCount = rx != NULL ? size : 0U;
    I2S_TypeDef *i2s = handle->Instance;
    uint32_t control = BUS_READ(i2s->CTL) & ~0x107U;
    BUS_WRITE(i2s->FCTL, BUS_READ(i2s->FCTL) | I2S_FIFO_RESET);
    HAL_StatusTypeDef status = bus_wait_clear(&i2s->FCTL, I2S_FIFO_RESET);
    if (status != HAL_OK)
    {
        goto done;
    }
    BUS_WRITE(i2s->ISTA, I2S_ERRORS);
    if (size == 0U)
    {
        goto done;
    }
    /* Prefill before enabling TX to avoid a startup underrun. */
    uint32_t space = (BUS_READ(i2s->FSTA) >> 16) & 0xFFU;
    if (space > I2S_TX_FIFO_WORDS)
    {
        handle->ErrorCode = HAL_I2S_ERROR_FIFO;
        status = HAL_ERROR;
        goto done;
    }
    if (tx != NULL)
    {
        fill_tx(handle, tx, size, space);
    }
    BUS_WRITE(i2s->CTL, control | 1U | (tx != NULL ? 0x104U : 0U) | (rx != NULL ? 2U : 0U));
    uint32_t start = HAL_GetTick();
    while (handle->TxXferCount != 0U || handle->RxXferCount != 0U)
    {
        uint32_t errors = BUS_READ(i2s->ISTA) & I2S_ERRORS;
        if (tx == NULL || handle->TxXferCount == 0U)
        {
            errors &= ~HAL_I2S_ERROR_UDR;
        }
        if (rx == NULL)
        {
            errors &= ~(HAL_I2S_ERROR_OVR | (1U << 2));
        }
        if (errors != 0U)
        {
            handle->ErrorCode = errors;
            status = HAL_ERROR;
            goto done;
        }
        uint32_t fifo = BUS_READ(i2s->FSTA);
        space = (fifo >> 16) & 0xFFU;
        uint32_t available = fifo & 0x7FU;
        if (space > I2S_TX_FIFO_WORDS || available > I2S_RX_FIFO_WORDS)
        {
            handle->ErrorCode = HAL_I2S_ERROR_FIFO;
            status = HAL_ERROR;
            goto done;
        }
        /* Service the occupancy snapshot in bursts. A per-sample timebase
         * read/divide can starve TX at the BROM's low CPU clock. The CPU is
         * the only FIFO producer/consumer, so these counts are conservative
         * while the serializer continues to run. */
        if (handle->TxXferCount != 0U)
        {
            fill_tx(handle, tx, size, space);
        }
        uint32_t count = handle->RxXferCount < available ? handle->RxXferCount : available;
        for (uint32_t i = 0U; i < count; ++i)
        {
            rx[size - handle->RxXferCount] = BUS_READ(i2s->RXFIFO);
            --handle->RxXferCount;
        }
        if (bus_expired(start, timeout))
        {
            status = HAL_TIMEOUT;
            goto done;
        }
    }
    if (tx != NULL)
    {
        /* FSTA free-space == 128 means FIFO empty. TXCNT counts host writes,
         * not wire completion, so it must not be used to decide completion. */
        while (((BUS_READ(i2s->FSTA) >> 16) & 0xFFU) != I2S_TX_FIFO_WORDS)
        {
            if (bus_expired(start, timeout))
            {
                status = HAL_TIMEOUT;
                goto done;
            }
        }
        /* Allow the last stereo shift frame to leave the serializer. Two tick
         * edges guarantee >=1 ms even when the first is close to a tick edge;
         * at supported rates >=8 kHz this is longer than a whole frame. */
        uint32_t tail = HAL_GetTick();
        while ((uint32_t)(HAL_GetTick() - tail) < 2U)
        {
            if (bus_expired(start, timeout))
            {
                status = HAL_TIMEOUT;
                goto done;
            }
        }
    }
done:
    BUS_WRITE(i2s->CTL, control);
    BUS_WRITE(i2s->ISTA, I2S_ERRORS);
    if (status == HAL_TIMEOUT)
    {
        handle->ErrorCode |= HAL_I2S_ERROR_TIMEOUT;
    }
    handle->State = HAL_I2S_STATE_READY;
    if (status != HAL_OK)
    {
        (void)HAL_RCC_I2S_ForceReset(i2s);
        handle->State = HAL_I2S_STATE_RESET;
    }
    HAL_Unlock(&handle->Lock);
    return status;
}
HAL_StatusTypeDef HAL_I2S_Transmit(I2S_HandleTypeDef *h, const uint32_t *d, uint32_t s)
{
    const uint32_t t = polling_timeout;
    if (d == NULL && s != 0U)
    {
        return HAL_ERROR;
    }
    return transfer(h, d, NULL, s, t);
}
HAL_StatusTypeDef HAL_I2S_Receive(I2S_HandleTypeDef *h, uint32_t *d, uint32_t s)
{
    const uint32_t t = polling_timeout;
    if (d == NULL && s != 0U)
    {
        return HAL_ERROR;
    }
    return transfer(h, NULL, d, s, t);
}
HAL_StatusTypeDef HAL_I2S_TransmitReceive(I2S_HandleTypeDef *h, const uint32_t *tx, uint32_t *rx,
                                          uint32_t s)
{
    const uint32_t t = polling_timeout;
    if ((tx == NULL || rx == NULL) && s != 0U)
    {
        return HAL_ERROR;
    }
    return transfer(h, tx, rx, s, t);
}
__weak void HAL_I2S_MspInit(I2S_HandleTypeDef *handle)
{
    UNUSED(handle);
}
__weak void HAL_I2S_MspDeInit(I2S_HandleTypeDef *handle)
{
    UNUSED(handle);
}
