#include "r328_hal_bus_private.h"

static uint32_t polling_timeout = HAL_POLLING_DEFAULT_TIMEOUT_MS;
void HAL_SPI_SetPollingTimeout(uint32_t timeout)
{
    polling_timeout = timeout;
}
#define SPI_XCH (1UL << 31)
#define SPI_TC (1U << 12)
#define SPI_FIFO_ERRORS (0xFU << 8)
#define SPI_FIFO_RESET ((1UL << 31) | (1UL << 15))
#define SPI_FIFO_DEPTH 64U
#define SPI_SS_LEVEL (1U << 7)
#define SPI_DISCARD_RX (1U << 8)
#define SPI_TX_DRQ (1UL << 24)
#define SPI_RX_DRQ (1U << 8)
#define SPI_FIFO_THRESHOLDS ((32U << 16) | 1U)
#define SPI_ISR_CLEAR 0x3F77U
#define SPI_MAX_DMA_SIZE 0xFFFFFFU
#define SPI_ASYNC_IT 1U
#define SPI_ASYNC_DMA 2U
#define SPI_DMA_TX 1U
#define SPI_DMA_RX 2U
#define SPI_ASYNC_TIMEOUT_MS 100U
#define SPI_IRQ_PRIORITY 0x90U
#define SPI_SAMPLE_NORMAL (1U << 13)
#define SPI_SAMPLE_DELAY (1U << 11)
static void release_irq(SPI_HandleTypeDef *handle);

static int valid_instance(const SPI_TypeDef *instance)
{
    return instance == SPI0 || instance == SPI1;
}
typedef struct
{
    uint32_t source, divisor, ccr;
} SPI_ClockPlan;
static int clock_plan(uint32_t requested, SPI_ClockPlan *plan)
{
    if (requested == 0U || requested > SPI_MAX_BAUD_RATE_HZ)
    {
        return 0;
    }
    *plan = (SPI_ClockPlan){0};
    const uint32_t sources[] = {RCC_SPI_CLOCK_OSC24M, RCC_SPI_CLOCK_PERI0};
    for (uint32_t source = 0U; source < 2U; ++source)
    {
        /* Both CCR dividers: compare rational rates, never round above the limit.
         * Ties retain OSC24M so low-speed users need no peripheral PLL. */
        for (uint32_t code = 0U; code < 271U; ++code)
        {
            uint32_t divisor = code < 256U ? 2U * (code + 1U) : 1UL << (code - 255U);
            if ((uint64_t)requested * divisor >= sources[source] &&
                (plan->source == 0U ||
                 (uint64_t)sources[source] * plan->divisor > (uint64_t)plan->source * divisor))
            {
                *plan = (SPI_ClockPlan){sources[source], divisor,
                                        code < 256U ? (1U << 12) | code : (code - 255U) << 8};
            }
        }
    }
    return plan->source != 0U;
}
static uint32_t sample_mode(const SPI_ClockPlan *plan)
{
    if (plan->source <= (uint64_t)24000000U * plan->divisor)
    {
        return SPI_SAMPLE_NORMAL;
    }
    return plan->source >= (uint64_t)60000000U * plan->divisor ? SPI_SAMPLE_DELAY : 0U;
}
HAL_StatusTypeDef HAL_SPI_SetBaudRate(SPI_HandleTypeDef *handle, uint32_t baud_rate_hz)
{
    SPI_ClockPlan plan;
    if (handle == NULL || !valid_instance(handle->Instance) || !clock_plan(baud_rate_hz, &plan))
    {
        return HAL_ERROR;
    }
    if (HAL_TryLock(&handle->Lock) != HAL_OK)
    {
        return HAL_BUSY;
    }
    if (handle->State != HAL_SPI_STATE_READY || (BUS_READ(handle->Instance->TCR) & SPI_XCH) != 0U)
    {
        HAL_StatusTypeDef result = handle->State == HAL_SPI_STATE_RESET ? HAL_ERROR : HAL_BUSY;
        HAL_Unlock(&handle->Lock);
        return result;
    }
    HAL_StatusTypeDef result = HAL_RCC_SPI_ConfigClock(handle->Instance, plan.source);
    if (result == HAL_OK)
    {
        BUS_WRITE(handle->Instance->CCR, plan.ccr);
        BUS_WRITE(handle->Instance->TCR,
                  (BUS_READ(handle->Instance->TCR) & ~(SPI_SAMPLE_NORMAL | SPI_SAMPLE_DELAY)) |
                      sample_mode(&plan));
        handle->Init.BaudRateHz = baud_rate_hz;
        handle->ActualBaudRateHz = plan.source / plan.divisor;
    }
    HAL_Unlock(&handle->Lock);
    return result;
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
    SPI_ClockPlan plan;
    if (init->Mode != SPI_MODE_MASTER || init->DataSize != SPI_DATASIZE_8BIT ||
        !clock_plan(init->BaudRateHz, &plan) || init->ChipSelect > 3U ||
        (init->CLKPolarity != SPI_POLARITY_LOW && init->CLKPolarity != SPI_POLARITY_HIGH) ||
        (init->CLKPhase != SPI_PHASE_1EDGE && init->CLKPhase != SPI_PHASE_2EDGE) ||
        (init->FirstBit != SPI_FIRSTBIT_MSB && init->FirstBit != SPI_FIRSTBIT_LSB) ||
        (init->NSS != SPI_NSS_SOFT && init->NSS != SPI_NSS_HARD_OUTPUT))
    {
        return HAL_ERROR;
    }
    if (HAL_TryLock(&handle->Lock) != HAL_OK)
    {
        return HAL_BUSY;
    }
    if (handle->State == HAL_SPI_STATE_BUSY)
    {
        HAL_Unlock(&handle->Lock);
        return HAL_BUSY;
    }
    if (handle->State == HAL_SPI_STATE_RESET)
    {
        HAL_SPI_MspInit(handle);
    }
    HAL_StatusTypeDef status = HAL_RCC_SPI_ConfigClock(handle->Instance, plan.source);
    if (status != HAL_OK)
    {
        HAL_Unlock(&handle->Lock);
        return status;
    }
    (void)HAL_RCC_SPI_ForceReset(handle->Instance);
    BUS_WRITE(handle->Instance->GCR, 0x83U); /* pause on RX full, master, enable */
    BUS_WRITE(handle->Instance->TCR, init->CLKPolarity | init->CLKPhase | init->FirstBit |
                                         sample_mode(&plan) | (1U << 9) | (1U << 7) | (1U << 6) |
                                         (1U << 2) | (init->ChipSelect << 4));
    BUS_WRITE(handle->Instance->CCR, plan.ccr);
    BUS_WRITE(handle->Instance->IER, 0U);
    BUS_WRITE(handle->Instance->WCR, 0U);
    BUS_WRITE(handle->Instance->ISR, 0x3F77U); /* direct W1C */
    handle->ErrorCode = HAL_SPI_ERROR_NONE;
    handle->AsyncMode = 0U;
    handle->XferTimeout = SPI_ASYNC_TIMEOUT_MS;
    handle->ActualBaudRateHz = plan.source / plan.divisor;
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
    release_irq(handle);
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
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *handle, const uint8_t *data, uint32_t size)
{
    const uint32_t timeout = polling_timeout;
    if (data == NULL && size != 0U)
    {
        return HAL_ERROR;
    }
    return transfer(handle, data, NULL, size, timeout);
}
HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *handle, uint8_t *data, uint32_t size)
{
    const uint32_t timeout = polling_timeout;
    if (data == NULL && size != 0U)
    {
        return HAL_ERROR;
    }
    return transfer(handle, NULL, data, size, timeout);
}
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *handle, const uint8_t *tx, uint8_t *rx,
                                          uint32_t size)
{
    const uint32_t timeout = polling_timeout;
    if ((tx == NULL || rx == NULL) && size != 0U)
    {
        return HAL_ERROR;
    }
    return transfer(handle, tx, rx, size, timeout);
}

static SPI_HandleTypeDef *irq_owners[2];
static IRQn_Type spi_irq(const SPI_HandleTypeDef *h)
{
    return h->Instance == SPI0 ? SPI0_IRQn : SPI1_IRQn;
}
static void spi_dispatch(void *context)
{
    HAL_SPI_IRQHandler(context);
}
static void release_irq(SPI_HandleTypeDef *h)
{
    uint32_t index = h->Instance == SPI0 ? 0U : 1U;
    if (irq_owners[index] == h)
    {
        (void)HAL_GIC_UnregisterIRQ(spi_irq(h), h);
        irq_owners[index] = NULL;
    }
}
static void configure_burst(SPI_HandleTypeDef *h, uint32_t size)
{
    SPI_TypeDef *spi = h->Instance;
    uint32_t tcr = BUS_READ(spi->TCR) & ~SPI_DISCARD_RX;
    if (h->RxBuffer == NULL)
    {
        tcr |= SPI_DISCARD_RX;
    }
    BUS_WRITE(spi->TCR, tcr);
    BUS_WRITE(spi->MBC, size);
    BUS_WRITE(spi->MTC, h->TxBuffer != NULL ? size : 0U);
    BUS_WRITE(spi->BCC, h->TxBuffer != NULL ? size : 0U);
}
static HAL_StatusTypeDef start_it_batch(SPI_HandleTypeDef *h)
{
    SPI_TypeDef *spi = h->Instance;
    h->BatchSize = h->XferCount > SPI_FIFO_DEPTH ? SPI_FIFO_DEPTH : h->XferCount;
    BUS_WRITE(spi->FCR, SPI_FIFO_RESET | SPI_FIFO_THRESHOLDS);
    if (bus_wait_clear(&spi->FCR, SPI_FIFO_RESET) != HAL_OK)
    {
        return HAL_TIMEOUT;
    }
    BUS_WRITE(spi->ISR, SPI_ISR_CLEAR);
    configure_burst(h, h->BatchSize);
    if (h->TxBuffer != NULL)
    {
        for (uint32_t i = 0U; i < h->BatchSize; ++i)
        {
            BUS_WRITE8(spi->TXD, h->TxBuffer[h->Offset + i]);
        }
    }
    BUS_WRITE(spi->IER, SPI_TC | SPI_FIFO_ERRORS);
    BUS_WRITE(spi->TCR, BUS_READ(spi->TCR) | SPI_XCH);
    return HAL_OK;
}
static void dma_complete(DMA_HandleTypeDef *dma)
{
    SPI_HandleTypeDef *h = dma->Parent;
    if (h != NULL && h->AsyncMode == SPI_ASYNC_DMA)
    {
        if (dma->ErrorCode != HAL_DMA_ERROR_NONE)
        {
            h->ErrorCode |= HAL_SPI_ERROR_DMA;
        }
        else
        {
            h->DmaDone |= dma == h->hdmatx ? SPI_DMA_TX : SPI_DMA_RX;
        }
    }
}
static HAL_StatusTypeDef start_dma(SPI_HandleTypeDef *h)
{
    SPI_TypeDef *spi = h->Instance;
    HAL_StatusTypeDef result = HAL_OK;
    configure_burst(h, h->XferSize);
    /* Controller-driven active, acknowledge before deassertion, five cycles.
     * Pairs with system DMA source/destination handshake mode. */
    BUS_WRITE(spi->NDMA_MODE, (3U << 6) | (1U << 5) | 5U);
    /* Arm RX first. It must be ready before the first clock edge. */
    if (h->RxBuffer != NULL)
    {
        h->hdmarx->Parent = h;
        h->hdmarx->XferCpltCallback = dma_complete;
        result =
            HAL_DMA_Start_IT(h->hdmarx, (uintptr_t)&spi->RXD, (uintptr_t)h->RxBuffer, h->XferSize);
        if (result != HAL_OK)
        {
            return result;
        }
        h->DmaActive |= SPI_DMA_RX;
    }
    if (h->TxBuffer != NULL)
    {
        h->hdmatx->Parent = h;
        h->hdmatx->XferCpltCallback = dma_complete;
        result =
            HAL_DMA_Start_IT(h->hdmatx, (uintptr_t)h->TxBuffer, (uintptr_t)&spi->TXD, h->XferSize);
        if (result != HAL_OK)
        {
            return result;
        }
        h->DmaActive |= SPI_DMA_TX;
    }
    BUS_WRITE(spi->FCR, SPI_FIFO_THRESHOLDS | (h->TxBuffer != NULL ? SPI_TX_DRQ : 0U) |
                            (h->RxBuffer != NULL ? SPI_RX_DRQ : 0U));
    if (h->TxBuffer != NULL)
    {
        /* Briefly wait for hardware prefill, never start XCH with empty TX.
         * TX trigger is 32 bytes; long transfers refill as clocks progress. */
        uint32_t prefill = h->XferSize < 32U ? h->XferSize : 32U;
        uint32_t count;
        for (count = 0U; count < HAL_REGISTER_POLL_LIMIT; ++count)
        {
            if (((BUS_READ(spi->FSR) >> 16) & 0xFFU) >= prefill)
            {
                break;
            }
        }
        if (count == HAL_REGISTER_POLL_LIMIT)
        {
            return HAL_TIMEOUT;
        }
    }
    BUS_WRITE(spi->IER, SPI_TC | SPI_FIFO_ERRORS);
    BUS_WRITE(spi->TCR, BUS_READ(spi->TCR) | SPI_XCH);
    return HAL_OK;
}
/* IRQs masked. Retain ownership until every started DMA channel has stopped. */
static int async_finish(SPI_HandleTypeDef *h)
{
    SPI_TypeDef *spi = h->Instance;
    BUS_WRITE(spi->IER, 0U);
    BUS_WRITE(spi->FCR, BUS_READ(spi->FCR) & ~(SPI_TX_DRQ | SPI_RX_DRQ));
    if (h->ErrorCode != HAL_SPI_ERROR_NONE)
    {
        (void)HAL_RCC_SPI_ForceReset(spi);
    }
    if (h->AsyncMode == SPI_ASYNC_DMA)
    {
        int stopped = 1;
        if ((h->DmaActive & SPI_DMA_RX) != 0U && HAL_DMA_Abort(h->hdmarx) != HAL_OK)
        {
            stopped = 0;
        }
        if ((h->DmaActive & SPI_DMA_TX) != 0U && HAL_DMA_Abort(h->hdmatx) != HAL_OK)
        {
            stopped = 0;
        }
        if (!stopped)
        {
            h->ErrorCode |= HAL_SPI_ERROR_DMA;
            return 0;
        }
    }
    BUS_WRITE(spi->ISR, SPI_ISR_CLEAR);
    h->AsyncMode = 0U;
    h->DmaActive = 0U;
    (void)finish(h, h->ErrorCode != HAL_SPI_ERROR_NONE ? HAL_ERROR : HAL_OK);
    return 1;
}
static void notify(SPI_HandleTypeDef *h)
{
    if (h->ErrorCode != HAL_SPI_ERROR_NONE)
    {
        HAL_SPI_ErrorCallback(h);
    }
    else if (h->TxBuffer == NULL)
    {
        HAL_SPI_RxCpltCallback(h);
    }
    else if (h->RxBuffer == NULL)
    {
        HAL_SPI_TxCpltCallback(h);
    }
    else
    {
        HAL_SPI_TxRxCpltCallback(h);
    }
}
static int valid_dma(DMA_HandleTypeDef *dma, uint32_t direction, uint32_t request)
{
    return dma != NULL && dma->State == HAL_DMA_STATE_READY && dma->Init.Direction == direction &&
           dma->Init.Request == request;
}
static HAL_StatusTypeDef async_start(SPI_HandleTypeDef *h, const uint8_t *tx, uint8_t *rx,
                                     uint32_t size, uint8_t mode)
{
    if (h == NULL || !valid_instance(h->Instance) || h->State == HAL_SPI_STATE_RESET ||
        (tx == NULL && rx == NULL) || size == 0U ||
        (mode == SPI_ASYNC_DMA && size > SPI_MAX_DMA_SIZE))
    {
        return HAL_ERROR;
    }
    if (mode == SPI_ASYNC_DMA && tx != NULL && rx != NULL)
    {
        uintptr_t t = (uintptr_t)tx, r = (uintptr_t)rx;
        if ((t < r ? r - t : t - r) < size)
        {
            return HAL_ERROR; /* DMA cannot receive over data not yet transmitted. */
        }
    }
    uint32_t state = R328_IRQ_Save();
    if (HAL_TryLock(&h->Lock) != HAL_OK)
    {
        R328_IRQ_Restore(state);
        return HAL_BUSY;
    }
    uint32_t request = h->Instance == SPI0 ? DMA_REQUEST_SPI0 : DMA_REQUEST_SPI1;
    if (mode == SPI_ASYNC_DMA &&
        ((tx != NULL && !valid_dma(h->hdmatx, DMA_MEMORY_TO_PERIPH, request)) ||
         (rx != NULL && !valid_dma(h->hdmarx, DMA_PERIPH_TO_MEMORY, request)) ||
         (tx != NULL && rx != NULL &&
          (h->hdmatx == h->hdmarx || h->hdmatx->Init.Channel == h->hdmarx->Init.Channel))))
    {
        HAL_Unlock(&h->Lock);
        R328_IRQ_Restore(state);
        return HAL_ERROR;
    }
    HAL_StatusTypeDef result = HAL_GIC_RegisterIRQ(spi_irq(h), spi_dispatch, h, SPI_IRQ_PRIORITY);
    if (result == HAL_OK)
    {
        result = HAL_GIC_EnableIRQ(spi_irq(h));
    }
    if (result != HAL_OK)
    {
        HAL_Unlock(&h->Lock);
        R328_IRQ_Restore(state);
        return result;
    }
    irq_owners[h->Instance == SPI0 ? 0U : 1U] = h;
    h->State = HAL_SPI_STATE_BUSY;
    h->ErrorCode = HAL_SPI_ERROR_NONE;
    h->TxBuffer = tx;
    h->RxBuffer = rx;
    h->XferSize = size;
    h->XferCount = size;
    h->Offset = 0U;
    h->BusDone = 0U;
    h->DmaDone = 0U;
    h->DmaActive = 0U;
    h->AsyncMode = mode;
    h->XferStart = HAL_GetTick();
    SPI_TypeDef *spi = h->Instance;
    BUS_WRITE(spi->IER, 0U);
    BUS_WRITE(spi->FCR, SPI_FIFO_RESET | SPI_FIFO_THRESHOLDS);
    result = bus_wait_clear(&spi->FCR, SPI_FIFO_RESET);
    if (result == HAL_OK)
    {
        BUS_WRITE(spi->ISR, SPI_ISR_CLEAR);
        if (h->Init.NSS == SPI_NSS_HARD_OUTPUT)
        {
            BUS_WRITE(spi->TCR, BUS_READ(spi->TCR) & ~SPI_SS_LEVEL);
        }
        result = mode == SPI_ASYNC_IT ? start_it_batch(h) : start_dma(h);
    }
    if (result != HAL_OK)
    {
        h->ErrorCode = result == HAL_TIMEOUT ? HAL_SPI_ERROR_TIMEOUT : HAL_SPI_ERROR_DMA;
        /* Usually no callback on start failure. If DMA cannot stop, retain
         * BUSY/buffers and let Process retry cleanup and report the error. */
        if (!async_finish(h))
        {
            result = HAL_TIMEOUT;
        }
    }
    R328_IRQ_Restore(state);
    return result;
}
void HAL_SPI_IRQHandler(SPI_HandleTypeDef *h)
{
    if (h == NULL || !valid_instance(h->Instance) || h->AsyncMode == 0U ||
        h->State != HAL_SPI_STATE_BUSY || h->ErrorCode != HAL_SPI_ERROR_NONE)
    {
        return;
    }
    SPI_TypeDef *spi = h->Instance;
    uint32_t pending = BUS_READ(spi->ISR) & BUS_READ(spi->IER);
    if ((pending & SPI_FIFO_ERRORS) != 0U)
    {
        h->ErrorCode |= HAL_SPI_ERROR_FIFO;
        BUS_WRITE(spi->IER, 0U);
        BUS_WRITE(spi->ISR, pending);
        return;
    }
    if ((pending & SPI_TC) == 0U)
    {
        return;
    }
    BUS_WRITE(spi->IER, 0U);
    BUS_WRITE(spi->ISR, SPI_TC);
    if (h->AsyncMode == SPI_ASYNC_DMA)
    {
        h->BusDone = 1U;
        return;
    }
    if ((BUS_READ(spi->TCR) & SPI_XCH) != 0U ||
        (h->RxBuffer != NULL && (BUS_READ(spi->FSR) & 0xFFU) != h->BatchSize))
    {
        h->ErrorCode |= HAL_SPI_ERROR_FIFO;
        return;
    }
    if (h->RxBuffer != NULL)
    {
        for (uint32_t i = 0U; i < h->BatchSize; ++i)
        {
            h->RxBuffer[h->Offset + i] = BUS_READ8(spi->RXD);
        }
    }
    h->Offset += h->BatchSize;
    h->XferCount -= h->BatchSize;
    if (h->XferCount == 0U)
    {
        h->BusDone = 1U;
    }
    else if (start_it_batch(h) != HAL_OK)
    {
        h->ErrorCode |= HAL_SPI_ERROR_TIMEOUT;
    }
}
void HAL_SPI_Process(SPI_HandleTypeDef *h)
{
    if (h == NULL || !valid_instance(h->Instance))
    {
        return;
    }
    uint32_t state = R328_IRQ_Save();
    if (h->AsyncMode == 0U || h->State != HAL_SPI_STATE_BUSY)
    {
        R328_IRQ_Restore(state);
        return;
    }
    if (bus_expired(h->XferStart, h->XferTimeout))
    {
        h->ErrorCode |= HAL_SPI_ERROR_TIMEOUT;
    }
    if ((BUS_READ(h->Instance->ISR) & SPI_FIFO_ERRORS) != 0U)
    {
        h->ErrorCode |= HAL_SPI_ERROR_FIFO;
    }
    int complete = h->BusDone != 0U && (BUS_READ(h->Instance->TCR) & SPI_XCH) == 0U;
    if (complete && h->AsyncMode == SPI_ASYNC_DMA)
    {
        complete = h->DmaDone == h->DmaActive;
        if (complete)
        {
            uint32_t fifo = BUS_READ(h->Instance->FSR);
            if ((fifo & 0x00FF00FFU) != 0U)
            {
                h->ErrorCode |= HAL_SPI_ERROR_DMA;
            }
            h->XferCount = 0U;
        }
    }
    int done = (complete || h->ErrorCode != HAL_SPI_ERROR_NONE) && async_finish(h);
    R328_IRQ_Restore(state);
    if (done)
    {
        notify(h);
    }
}
HAL_StatusTypeDef HAL_SPI_SetAsyncTimeout(SPI_HandleTypeDef *h, uint32_t timeout)
{
    if (h == NULL || !valid_instance(h->Instance) || timeout == 0U)
    {
        return HAL_ERROR;
    }
    uint32_t state = R328_IRQ_Save();
    HAL_StatusTypeDef result = h->State == HAL_SPI_STATE_READY ? HAL_OK : HAL_BUSY;
    if (result == HAL_OK)
    {
        h->XferTimeout = timeout;
    }
    R328_IRQ_Restore(state);
    return result;
}
HAL_StatusTypeDef HAL_SPI_Abort(SPI_HandleTypeDef *h)
{
    if (h == NULL || !valid_instance(h->Instance))
    {
        return HAL_ERROR;
    }
    uint32_t state = R328_IRQ_Save();
    if (h->AsyncMode == 0U || h->State != HAL_SPI_STATE_BUSY)
    {
        R328_IRQ_Restore(state);
        return HAL_BUSY;
    }
    h->ErrorCode |= HAL_SPI_ERROR_ABORT;
    int done = async_finish(h);
    R328_IRQ_Restore(state);
    if (done)
    {
        notify(h);
    }
    return done ? HAL_OK : HAL_TIMEOUT;
}
HAL_StatusTypeDef HAL_SPI_Transmit_IT(SPI_HandleTypeDef *h, const uint8_t *data, uint32_t size)
{
    return async_start(h, data, NULL, size, SPI_ASYNC_IT);
}
HAL_StatusTypeDef HAL_SPI_Receive_IT(SPI_HandleTypeDef *h, uint8_t *data, uint32_t size)
{
    return async_start(h, NULL, data, size, SPI_ASYNC_IT);
}
HAL_StatusTypeDef HAL_SPI_TransmitReceive_IT(SPI_HandleTypeDef *h, const uint8_t *tx, uint8_t *rx,
                                             uint32_t size)
{
    return tx == NULL || rx == NULL ? HAL_ERROR : async_start(h, tx, rx, size, SPI_ASYNC_IT);
}
HAL_StatusTypeDef HAL_SPI_Transmit_DMA(SPI_HandleTypeDef *h, const uint8_t *data, uint32_t size)
{
    return async_start(h, data, NULL, size, SPI_ASYNC_DMA);
}
HAL_StatusTypeDef HAL_SPI_Receive_DMA(SPI_HandleTypeDef *h, uint8_t *data, uint32_t size)
{
    return async_start(h, NULL, data, size, SPI_ASYNC_DMA);
}
HAL_StatusTypeDef HAL_SPI_TransmitReceive_DMA(SPI_HandleTypeDef *h, const uint8_t *tx, uint8_t *rx,
                                              uint32_t size)
{
    return tx == NULL || rx == NULL ? HAL_ERROR : async_start(h, tx, rx, size, SPI_ASYNC_DMA);
}
__weak void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *h)
{
    UNUSED(h);
}
__weak void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *h)
{
    UNUSED(h);
}
__weak void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *h)
{
    UNUSED(h);
}
__weak void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *h)
{
    UNUSED(h);
}
__weak void HAL_SPI_MspInit(SPI_HandleTypeDef *handle)
{
    UNUSED(handle);
}
__weak void HAL_SPI_MspDeInit(SPI_HandleTypeDef *handle)
{
    UNUSED(handle);
}
