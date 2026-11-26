#include "r328_hal_bus_private.h"

static uint32_t polling_timeout = HAL_POLLING_DEFAULT_TIMEOUT_MS;
void HAL_I2C_SetPollingTimeout(uint32_t timeout)
{
    polling_timeout = timeout;
}
#define TWI_BUS_EN (1U << 6)
#define TWI_START (1U << 5)
#define TWI_STOP (1U << 4)
#define TWI_IF (1U << 3)
#define TWI_ACK (1U << 2)
#define TWI_INT_EN (1U << 7)
static void release_irq(I2C_HandleTypeDef *handle);
static int valid_instance(const I2C_TypeDef *instance)
{
    return instance == I2C0 || instance == I2C1;
}
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *handle)
{
    if (handle == NULL || !valid_instance(handle->Instance) || handle->Init.ClockSpeed == 0U ||
        handle->Init.ClockSpeed > 400000U || handle->Init.AddressingMode != I2C_ADDRESSINGMODE_7BIT)
    {
        return HAL_ERROR;
    }
    if (handle->State == HAL_I2C_STATE_BUSY)
    {
        return HAL_BUSY;
    }
    uint32_t pclk = HAL_RCC_GetPCLK2Freq(), best_divisor = UINT32_MAX, ccr = 0U;
    if (pclk == 0U)
    {
        return HAL_ERROR;
    }
    for (uint32_t n = 0U; n < 8U; ++n)
    {
        for (uint32_t m = 0U; m < 16U; ++m)
        {
            uint32_t divisor = 10U * (1UL << n) * (m + 1U);
            if ((uint64_t)handle->Init.ClockSpeed * divisor >= pclk && divisor < best_divisor)
            {
                best_divisor = divisor;
                ccr = (m << 3) | n;
            }
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
    if (handle->State == HAL_I2C_STATE_RESET)
    {
        HAL_I2C_MspInit(handle);
    }
    (void)HAL_RCC_I2C_EnableClock(handle->Instance);
    (void)HAL_RCC_I2C_ForceReset(handle->Instance);
    I2C_TypeDef *twi = handle->Instance;
    BUS_WRITE(twi->DRV_CTRL, 0U); /* Use TWI engine, not the separate FIFO driver. */
    BUS_WRITE(twi->SRST, 1U);
    HAL_StatusTypeDef status = bus_wait_clear(&twi->SRST, 1U);
    if (status == HAL_OK)
    {
        BUS_WRITE(twi->CCR, ccr);
        BUS_WRITE(twi->ADDR, 0U);
        BUS_WRITE(twi->XADDR, 0U);
        BUS_WRITE(twi->EFR, 0U);
        BUS_WRITE(twi->CNTR, TWI_BUS_EN | TWI_IF);
        handle->ActualClockSpeed = pclk / best_divisor;
        handle->AsyncMode = 0U;
        handle->XferTimeout = 100U;
    }
    handle->ErrorCode = status == HAL_OK ? HAL_I2C_ERROR_NONE : HAL_I2C_ERROR_TIMEOUT;
    handle->State = status == HAL_OK ? HAL_I2C_STATE_READY : HAL_I2C_STATE_RESET;
    HAL_Unlock(&handle->Lock);
    return status;
}
HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *handle)
{
    if (handle == NULL || !valid_instance(handle->Instance))
    {
        return HAL_ERROR;
    }
    if (HAL_TryLock(&handle->Lock) != HAL_OK)
    {
        return HAL_BUSY;
    }
    (void)HAL_RCC_I2C_ForceReset(handle->Instance);
    HAL_I2C_MspDeInit(handle);
    release_irq(handle);
    handle->State = HAL_I2C_STATE_RESET;
    HAL_Unlock(&handle->Lock);
    return HAL_OK;
}
static HAL_StatusTypeDef step(I2C_HandleTypeDef *handle, uint32_t control, uint32_t expected,
                              uint32_t start, uint32_t timeout)
{
    I2C_TypeDef *twi = handle->Instance;
    /* INT_FLAG is W1C. Never use read/modify/write for this register. */
    BUS_WRITE(twi->CNTR, TWI_BUS_EN | TWI_IF | control);
    while ((BUS_READ(twi->CNTR) & TWI_IF) == 0U)
    {
        if (bus_expired(start, timeout))
        {
            handle->ErrorCode |= HAL_I2C_ERROR_TIMEOUT;
            return HAL_TIMEOUT;
        }
    }
    uint32_t state = BUS_READ(twi->STAT) & 0xFFU;
    if (state == expected)
    {
        return HAL_OK;
    }
    if (state == 0x20U || state == 0x30U || state == 0x48U)
    {
        handle->ErrorCode |= HAL_I2C_ERROR_AF;
    }
    else if (state == 0x38U)
    {
        handle->ErrorCode |= HAL_I2C_ERROR_ARLO;
    }
    else
    {
        handle->ErrorCode |= HAL_I2C_ERROR_BERR;
    }
    return HAL_ERROR;
}
static HAL_StatusTypeDef address_phase(I2C_HandleTypeDef *handle, uint16_t address, int read,
                                       int repeated, uint32_t start, uint32_t timeout)
{
    HAL_StatusTypeDef status = step(handle, TWI_START, repeated ? 0x10U : 0x08U, start, timeout);
    if (status != HAL_OK)
    {
        return status;
    }
    BUS_WRITE(handle->Instance->DATA, address | (read ? 1U : 0U));
    return step(handle, 0U, read ? 0x40U : 0x18U, start, timeout);
}
static HAL_StatusTypeDef send_byte(I2C_HandleTypeDef *handle, uint8_t data, uint32_t start,
                                   uint32_t timeout)
{
    BUS_WRITE(handle->Instance->DATA, data);
    return step(handle, 0U, 0x28U, start, timeout);
}
static HAL_StatusTypeDef finish(I2C_HandleTypeDef *handle, HAL_StatusTypeDef status, uint32_t start,
                                uint32_t timeout)
{
    I2C_TypeDef *twi = handle->Instance;
    /* Never generate STOP after losing arbitration: the winner owns the bus. */
    if ((handle->ErrorCode & HAL_I2C_ERROR_ARLO) == 0U)
    {
        BUS_WRITE(twi->CNTR, TWI_BUS_EN | TWI_IF | TWI_STOP);
        while ((BUS_READ(twi->CNTR) & TWI_STOP) != 0U || (BUS_READ(twi->STAT) & 0xFFU) != 0xF8U)
        {
            if (bus_expired(start, timeout))
            {
                handle->ErrorCode |= HAL_I2C_ERROR_TIMEOUT;
                if (status == HAL_OK)
                {
                    status = HAL_TIMEOUT;
                }
                break;
            }
        }
    }
    else
    {
        BUS_WRITE(twi->CNTR, TWI_BUS_EN | TWI_IF);
    }
    if ((handle->ErrorCode & (HAL_I2C_ERROR_TIMEOUT | HAL_I2C_ERROR_BERR)) != 0U)
    {
        (void)HAL_RCC_I2C_ForceReset(twi);
        handle->State = HAL_I2C_STATE_RESET; /* Explicit Init required after a stuck bus. */
    }
    else
    {
        handle->State = HAL_I2C_STATE_READY;
    }
    HAL_Unlock(&handle->Lock);
    return status;
}
static HAL_StatusTypeDef transaction(I2C_HandleTypeDef *handle, uint16_t address,
                                     uint16_t mem_address, uint32_t mem_size, const uint8_t *tx,
                                     uint8_t *rx, uint32_t size, int read, uint32_t timeout)
{
    if (handle == NULL || !valid_instance(handle->Instance) ||
        handle->State == HAL_I2C_STATE_RESET || address > 0xFEU || (address & 1U) != 0U ||
        mem_size > 2U || (size != 0U && (read ? rx == NULL : tx == NULL)))
    {
        return HAL_ERROR;
    }
    if (HAL_TryLock(&handle->Lock) != HAL_OK)
    {
        return HAL_BUSY;
    }
    handle->State = HAL_I2C_STATE_BUSY;
    handle->ErrorCode = HAL_I2C_ERROR_NONE;
    uint32_t start = HAL_GetTick();
    BUS_WRITE(handle->Instance->DRV_CTRL, 0U);
    HAL_StatusTypeDef status =
        address_phase(handle, address, read && mem_size == 0U, 0, start, timeout);
    if (status == HAL_OK && mem_size == 2U)
    {
        status = send_byte(handle, (uint8_t)(mem_address >> 8), start, timeout);
    }
    if (status == HAL_OK && mem_size != 0U)
    {
        status = send_byte(handle, (uint8_t)mem_address, start, timeout);
    }
    if (status == HAL_OK && mem_size != 0U && read)
    {
        status = address_phase(handle, address, 1, 1, start, timeout);
    }
    for (uint32_t i = 0U; status == HAL_OK && i < size; ++i)
    {
        if (read)
        {
            int last = i + 1U == size;
            status = step(handle, last ? 0U : TWI_ACK, last ? 0x58U : 0x50U, start, timeout);
            if (status == HAL_OK)
            {
                rx[i] = (uint8_t)BUS_READ(handle->Instance->DATA);
            }
        }
        else
        {
            status = send_byte(handle, tx[i], start, timeout);
        }
    }
    return finish(handle, status, start, timeout);
}
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *h, uint16_t a, const uint8_t *d,
                                          uint32_t s)
{
    const uint32_t t = polling_timeout;
    return transaction(h, a, 0U, 0U, d, NULL, s, 0, t);
}
HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *h, uint16_t a, uint8_t *d, uint32_t s)
{
    const uint32_t t = polling_timeout;
    if (s == 0U)
    {
        return HAL_ERROR;
    }
    return transaction(h, a, 0U, 0U, NULL, d, s, 1, t);
}
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *h, uint16_t a, uint16_t m, uint32_t ms,
                                    const uint8_t *d, uint32_t s)
{
    const uint32_t t = polling_timeout;
    if ((ms != 1U && ms != 2U) || (ms == 1U && m > 0xFFU))
    {
        return HAL_ERROR;
    }
    return transaction(h, a, m, ms, d, NULL, s, 0, t);
}
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *h, uint16_t a, uint16_t m, uint32_t ms,
                                   uint8_t *d, uint32_t s)
{
    const uint32_t t = polling_timeout;
    if (s == 0U || (ms != 1U && ms != 2U) || (ms == 1U && m > 0xFFU))
    {
        return HAL_ERROR;
    }
    return transaction(h, a, m, ms, NULL, d, s, 1, t);
}
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *h, uint16_t a, uint32_t trials)
{
    const uint32_t timeout = polling_timeout;
    if (trials == 0U)
    {
        return HAL_ERROR;
    }
    HAL_StatusTypeDef status = HAL_ERROR;
    for (uint32_t i = 0U; i < trials; ++i)
    {
        status = transaction(h, a, 0U, 0U, NULL, NULL, 0U, 0, timeout);
        if (status != HAL_ERROR || h == NULL || h->ErrorCode != HAL_I2C_ERROR_AF)
        {
            break;
        }
    }
    return status;
}

/* Asynchronous traditional-engine IRQ state machine and packet-engine DMA. */
#define ASYNC_IT 1U
#define ASYNC_DMA 2U
#define PHASE_START 1U
#define PHASE_ADDRESS 2U
#define PHASE_MEMORY 3U
#define PHASE_TX 4U
#define PHASE_RX 5U
#define PHASE_STOP 6U
#define DRV_START (1UL << 31)
#define DRV_IRQS (3UL << 16)
static I2C_HandleTypeDef *irq_owners[2];

static IRQn_Type i2c_irq(I2C_HandleTypeDef *h)
{
    return h->Instance == I2C0 ? TWI0_IRQn : TWI1_IRQn;
}
static void i2c_dispatch(void *context)
{
    HAL_I2C_IRQHandler(context);
}
static void release_irq(I2C_HandleTypeDef *h)
{
    uint32_t index = h->Instance == I2C0 ? 0U : 1U;
    if (irq_owners[index] == h)
    {
        (void)HAL_GIC_UnregisterIRQ(i2c_irq(h), h);
        irq_owners[index] = NULL;
    }
}
static uint32_t status_error(uint32_t state)
{
    if (state == 0x20U || state == 0x30U || state == 0x48U)
    {
        return HAL_I2C_ERROR_AF;
    }
    if (state == 0x38U)
    {
        return HAL_I2C_ERROR_ARLO;
    }
    return state == 1U ? HAL_I2C_ERROR_TIMEOUT : HAL_I2C_ERROR_BERR;
}
static void it_step(I2C_HandleTypeDef *h, uint32_t control, uint8_t expected, uint8_t phase)
{
    h->Expected = expected;
    h->Phase = phase;
    BUS_WRITE(h->Instance->CNTR, TWI_BUS_EN | TWI_INT_EN | TWI_IF | control);
}
static void it_stop(I2C_HandleTypeDef *h)
{
    h->Phase = PHASE_STOP;
    h->BusDone = 1U;
    BUS_WRITE(h->Instance->CNTR,
              TWI_BUS_EN | TWI_IF | ((h->ErrorCode & HAL_I2C_ERROR_ARLO) != 0U ? 0U : TWI_STOP));
}
static void it_next(I2C_HandleTypeDef *h)
{
    if (h->MemRemaining != 0U)
    {
        uint8_t byte =
            h->MemRemaining == 2U ? (uint8_t)(h->MemAddress >> 8) : (uint8_t)h->MemAddress;
        --h->MemRemaining;
        BUS_WRITE(h->Instance->DATA, byte);
        it_step(h, 0U, 0x28U, PHASE_MEMORY);
    }
    else if (h->Read != 0U && (h->StartAddress & 1U) == 0U)
    {
        h->StartAddress |= 1U;
        it_step(h, TWI_START, 0x10U, PHASE_START);
    }
    else if (h->Read != 0U)
    {
        it_step(h, h->XferCount > 1U ? TWI_ACK : 0U, h->XferCount > 1U ? 0x50U : 0x58U, PHASE_RX);
    }
    else if (h->XferCount != 0U)
    {
        BUS_WRITE(h->Instance->DATA, h->TxBuffer[h->XferSize - h->XferCount]);
        it_step(h, 0U, 0x28U, PHASE_TX);
    }
    else
    {
        it_stop(h);
    }
}
static void dma_complete(DMA_HandleTypeDef *dma)
{
    I2C_HandleTypeDef *h = dma->Parent;
    if (h != NULL && h->AsyncMode == ASYNC_DMA)
    {
        if (dma->ErrorCode != HAL_DMA_ERROR_NONE)
        {
            h->ErrorCode |= HAL_I2C_ERROR_DMA;
            return;
        }
        h->DmaDone = 1U;
        h->XferCount = 0U;
    }
}
static DMA_HandleTypeDef *active_dma(I2C_HandleTypeDef *h)
{
    return h->Read != 0U ? h->hdmarx : h->hdmatx;
}
/* Called with CPU0 IRQs masked; restore/release the handle before callbacks. */
static int async_finish(I2C_HandleTypeDef *h)
{
    I2C_TypeDef *twi = h->Instance;
    if (h->AsyncMode == ASYNC_DMA)
    {
        /* Both engines have completed. Remaining RX bytes mean the DMA count
         * was consumed before all received bytes reached memory. Never report
         * a successful read with silently truncated/corrupted data. */
        if (h->ErrorCode == HAL_I2C_ERROR_NONE && h->Read != 0U &&
            ((BUS_READ(twi->DRV_FIFO_CON) >> 16) & 0x3FU) != 0U)
        {
            h->ErrorCode |= HAL_I2C_ERROR_DMA;
        }
        BUS_WRITE(twi->DRV_DMA_CFG, 0U);
        DMA_HandleTypeDef *dma = active_dma(h);
        if (dma->State != HAL_DMA_STATE_READY && HAL_DMA_Abort(dma) != HAL_OK)
        {
            h->ErrorCode |= HAL_I2C_ERROR_DMA;
            return 0; /* Memory may still be DMA-owned. Keep BUSY and retain buffers. */
        }
    }
    BUS_WRITE(twi->DRV_INT_CTRL, 0xFU);
    BUS_WRITE(twi->DRV_CTRL, 0U);
    BUS_WRITE(twi->CNTR, TWI_BUS_EN | TWI_IF);
    if ((h->AsyncMode == ASYNC_DMA && h->ErrorCode != HAL_I2C_ERROR_NONE) ||
        (h->ErrorCode & (HAL_I2C_ERROR_BERR | HAL_I2C_ERROR_TIMEOUT | HAL_I2C_ERROR_DMA |
                         HAL_I2C_ERROR_ABORT)) != 0U)
    {
        (void)HAL_RCC_I2C_ForceReset(twi);
        h->State = HAL_I2C_STATE_RESET;
    }
    else
    {
        h->State = HAL_I2C_STATE_READY;
    }
    h->AsyncMode = 0U;
    HAL_Unlock(&h->Lock);
    return 1;
}
static void notify(I2C_HandleTypeDef *h)
{
    if (h->ErrorCode != HAL_I2C_ERROR_NONE)
    {
        HAL_I2C_ErrorCallback(h);
    }
    else if (h->Memory != 0U)
    {
        if (h->Read != 0U)
        {
            HAL_I2C_MemRxCpltCallback(h);
        }
        else
        {
            HAL_I2C_MemTxCpltCallback(h);
        }
    }
    else if (h->Read != 0U)
    {
        HAL_I2C_MasterRxCpltCallback(h);
    }
    else
    {
        HAL_I2C_MasterTxCpltCallback(h);
    }
}
static HAL_StatusTypeDef async_start(I2C_HandleTypeDef *h, uint16_t address, uint16_t mem,
                                     uint32_t mem_size, const uint8_t *tx, uint8_t *rx,
                                     uint32_t size, uint8_t read, uint8_t mode, uint8_t memory)
{
    if (h == NULL || !valid_instance(h->Instance) || h->State == HAL_I2C_STATE_RESET ||
        address > 0xFEU || (address & 1U) != 0U ||
        (memory != 0U && (mem_size < 1U || mem_size > 2U || (mem_size == 1U && mem > 0xFFU))) ||
        (size != 0U && (read != 0U ? rx == NULL : tx == NULL)) || (read != 0U && size == 0U) ||
        (mode == ASYNC_DMA && (size == 0U || size > 65535U)))
    {
        return HAL_ERROR;
    }
    uint32_t state = R328_IRQ_Save();
    if (HAL_TryLock(&h->Lock) != HAL_OK)
    {
        R328_IRQ_Restore(state);
        return HAL_BUSY;
    }
    DMA_HandleTypeDef *dma = read != 0U ? h->hdmarx : h->hdmatx;
    uint32_t request = h->Instance == I2C0 ? DMA_REQUEST_TWI0 : DMA_REQUEST_TWI1;
    if (mode == ASYNC_DMA &&
        (dma == NULL || dma->State != HAL_DMA_STATE_READY || dma->Init.Request != request ||
         dma->Init.Direction != (read != 0U ? DMA_PERIPH_TO_MEMORY : DMA_MEMORY_TO_PERIPH)))
    {
        HAL_Unlock(&h->Lock);
        R328_IRQ_Restore(state);
        return HAL_ERROR;
    }
    HAL_StatusTypeDef result = HAL_GIC_RegisterIRQ(i2c_irq(h), i2c_dispatch, h, 0x90U);
    if (result == HAL_OK)
    {
        result = HAL_GIC_EnableIRQ(i2c_irq(h));
    }
    if (result != HAL_OK)
    {
        HAL_Unlock(&h->Lock);
        R328_IRQ_Restore(state);
        return result;
    }
    irq_owners[h->Instance == I2C0 ? 0U : 1U] = h;
    h->State = HAL_I2C_STATE_BUSY;
    h->ErrorCode = HAL_I2C_ERROR_NONE;
    h->TxBuffer = tx;
    h->RxBuffer = rx;
    h->XferSize = size;
    h->XferCount = size;
    h->Read = read;
    h->Memory = memory;
    h->MemSize = (uint8_t)mem_size;
    h->MemRemaining = (uint8_t)mem_size;
    h->MemAddress = mem;
    h->DeviceAddress = address;
    h->StartAddress = (uint8_t)(address | (read != 0U && mem_size == 0U ? 1U : 0U));
    h->AsyncMode = mode;
    h->DmaDone = 0U;
    h->BusDone = 0U;
    h->XferStart = HAL_GetTick();
    I2C_TypeDef *twi = h->Instance;
    BUS_WRITE(twi->DRV_INT_CTRL, 0xFU);
    BUS_WRITE(twi->DRV_CTRL, 0U);
    if (mode == ASYNC_IT)
    {
        it_step(h, TWI_START, 0x08U, PHASE_START);
    }
    else
    {
        BUS_WRITE(twi->CNTR, TWI_IF); /* Disable software engine IRQ and bus driver. */
        BUS_WRITE(twi->DRV_CTRL, 3U);
        BUS_WRITE(twi->DRV_CTRL, 1U);
        BUS_WRITE(twi->DRV_FIFO_CON, (1UL << 22) | (1U << 6));
        result = bus_wait_clear(&twi->DRV_FIFO_CON, (1UL << 22) | (1U << 6));
        if (result == HAL_OK)
        {
            uint32_t ccr = BUS_READ(twi->CCR);
            BUS_WRITE(twi->DRV_BUS_CTRL, ((ccr & 7U) << 12) | (((ccr >> 3) & 15U) << 8));
            BUS_WRITE(twi->DRV_CFG, (0x10UL << 24) | 1U);
            BUS_WRITE(twi->DRV_SLV, ((uint32_t)address << 8) | ((uint32_t)read << 8));
            BUS_WRITE(twi->DRV_FMT, (mem_size << 16) | size);
            if (mem_size == 2U)
            {
                BUS_WRITE8(twi->DRV_SEND_FIFO, (uint8_t)(mem >> 8));
            }
            if (mem_size != 0U)
            {
                BUS_WRITE8(twi->DRV_SEND_FIFO, (uint8_t)mem);
            }
            dma->Parent = h;
            dma->XferCpltCallback = dma_complete;
            result =
                HAL_DMA_Start_IT(dma, read != 0U ? (uintptr_t)&twi->DRV_RECV_FIFO : (uintptr_t)tx,
                                 read != 0U ? (uintptr_t)rx : (uintptr_t)&twi->DRV_SEND_FIFO, size);
        }
        if (result == HAL_OK)
        {
            BUS_WRITE(twi->DRV_DMA_CFG, read != 0U ? (1UL << 24) | (1UL << 16) : (1U << 8) | 1U);
            BUS_WRITE(twi->DRV_INT_CTRL, DRV_IRQS | 0xFU);
            BUS_WRITE(twi->DRV_CTRL,
                      DRV_START | 1U | (read != 0U && mem_size == 0U ? 1UL << 28 : 0U));
        }
        else
        {
            /* The start did not succeed: no callback is delivered. */
            h->ErrorCode = result == HAL_TIMEOUT ? HAL_I2C_ERROR_TIMEOUT : HAL_I2C_ERROR_DMA;
            (void)async_finish(h);
        }
    }
    R328_IRQ_Restore(state);
    return result;
}
void HAL_I2C_IRQHandler(I2C_HandleTypeDef *h)
{
    if (h == NULL || h->State != HAL_I2C_STATE_BUSY)
    {
        return;
    }
    I2C_TypeDef *twi = h->Instance;
    if (h->AsyncMode == ASYNC_DMA)
    {
        uint32_t pending = BUS_READ(twi->DRV_INT_CTRL) & 3U;
        if (pending == 0U)
        {
            return;
        }
        BUS_WRITE(twi->DRV_INT_CTRL, pending); /* Disable IRQs and clear only asserted W1C bits. */
        uint32_t control = BUS_READ(twi->DRV_CTRL);
        if ((pending & 2U) != 0U || (control & (15UL << 24)) != 0U)
        {
            h->ErrorCode |= status_error((control >> 16) & 0xFFU);
        }
        h->BusDone = 1U;
        return;
    }
    if (h->AsyncMode != ASYNC_IT || h->Phase == PHASE_STOP ||
        (BUS_READ(twi->CNTR) & (TWI_INT_EN | TWI_IF)) != (TWI_INT_EN | TWI_IF))
    {
        return;
    }
    uint32_t status = BUS_READ(twi->STAT) & 0xFFU;
    if (status != h->Expected)
    {
        h->ErrorCode |= status_error(status);
        it_stop(h);
        return;
    }
    if (h->Phase == PHASE_START)
    {
        BUS_WRITE(twi->DATA, h->StartAddress);
        it_step(h, 0U, (h->StartAddress & 1U) != 0U ? 0x40U : 0x18U, PHASE_ADDRESS);
        return;
    }
    if (h->Phase == PHASE_TX)
    {
        --h->XferCount;
    }
    if (h->Phase == PHASE_RX)
    {
        h->RxBuffer[h->XferSize - h->XferCount] = (uint8_t)BUS_READ(twi->DATA);
        --h->XferCount;
        if (h->XferCount == 0U)
        {
            it_stop(h);
            return;
        }
    }
    it_next(h);
}
void HAL_I2C_Process(I2C_HandleTypeDef *h)
{
    if (h == NULL || !valid_instance(h->Instance))
    {
        return;
    }
    uint32_t state = R328_IRQ_Save();
    if (h->AsyncMode == 0U || h->State != HAL_I2C_STATE_BUSY)
    {
        R328_IRQ_Restore(state);
        return;
    }
    if (bus_expired(h->XferStart, h->XferTimeout))
    {
        h->ErrorCode |= HAL_I2C_ERROR_TIMEOUT;
    }
    int complete = h->ErrorCode != HAL_I2C_ERROR_NONE;
    if (!complete && h->BusDone != 0U)
    {
        if (h->AsyncMode == ASYNC_DMA)
        {
            complete = h->DmaDone != 0U && (BUS_READ(h->Instance->DRV_CTRL) & DRV_START) == 0U;
        }
        else
        {
            complete = (BUS_READ(h->Instance->CNTR) & TWI_STOP) == 0U &&
                       (BUS_READ(h->Instance->STAT) & 0xFFU) == 0xF8U;
        }
    }
    /* NACK needs STOP to finish; arbitration loss must not issue STOP. */
    if (h->AsyncMode == ASYNC_IT && h->ErrorCode == HAL_I2C_ERROR_AF)
    {
        complete = (BUS_READ(h->Instance->CNTR) & TWI_STOP) == 0U &&
                   (BUS_READ(h->Instance->STAT) & 0xFFU) == 0xF8U;
    }
    int done = complete && async_finish(h);
    R328_IRQ_Restore(state);
    if (done)
    {
        notify(h);
    }
}
HAL_StatusTypeDef HAL_I2C_SetAsyncTimeout(I2C_HandleTypeDef *h, uint32_t timeout)
{
    if (h == NULL || !valid_instance(h->Instance) || timeout == 0U)
    {
        return HAL_ERROR;
    }
    uint32_t state = R328_IRQ_Save();
    HAL_StatusTypeDef result = h->State == HAL_I2C_STATE_READY ? HAL_OK : HAL_BUSY;
    if (result == HAL_OK)
    {
        h->XferTimeout = timeout;
    }
    R328_IRQ_Restore(state);
    return result;
}
HAL_StatusTypeDef HAL_I2C_Abort(I2C_HandleTypeDef *h)
{
    if (h == NULL || !valid_instance(h->Instance))
    {
        return HAL_ERROR;
    }
    uint32_t state = R328_IRQ_Save();
    if (h->AsyncMode == 0U || h->State != HAL_I2C_STATE_BUSY)
    {
        R328_IRQ_Restore(state);
        return HAL_BUSY;
    }
    h->ErrorCode |= HAL_I2C_ERROR_ABORT;
    int done = async_finish(h);
    R328_IRQ_Restore(state);
    if (done)
    {
        notify(h);
    }
    return done ? HAL_OK : HAL_TIMEOUT;
}

HAL_StatusTypeDef HAL_I2C_Master_Transmit_IT(I2C_HandleTypeDef *h, uint16_t a, const uint8_t *d,
                                             uint32_t s)
{
    return async_start(h, a, 0U, 0U, d, NULL, s, 0U, ASYNC_IT, 0U);
}
HAL_StatusTypeDef HAL_I2C_Master_Receive_IT(I2C_HandleTypeDef *h, uint16_t a, uint8_t *d,
                                            uint32_t s)
{
    return async_start(h, a, 0U, 0U, NULL, d, s, 1U, ASYNC_IT, 0U);
}
HAL_StatusTypeDef HAL_I2C_Mem_Write_IT(I2C_HandleTypeDef *h, uint16_t a, uint16_t m, uint32_t ms,
                                       const uint8_t *d, uint32_t s)
{
    return async_start(h, a, m, ms, d, NULL, s, 0U, ASYNC_IT, 1U);
}
HAL_StatusTypeDef HAL_I2C_Mem_Read_IT(I2C_HandleTypeDef *h, uint16_t a, uint16_t m, uint32_t ms,
                                      uint8_t *d, uint32_t s)
{
    return async_start(h, a, m, ms, NULL, d, s, 1U, ASYNC_IT, 1U);
}
HAL_StatusTypeDef HAL_I2C_Master_Transmit_DMA(I2C_HandleTypeDef *h, uint16_t a, const uint8_t *d,
                                              uint32_t s)
{
    return async_start(h, a, 0U, 0U, d, NULL, s, 0U, ASYNC_DMA, 0U);
}
HAL_StatusTypeDef HAL_I2C_Master_Receive_DMA(I2C_HandleTypeDef *h, uint16_t a, uint8_t *d,
                                             uint32_t s)
{
    return async_start(h, a, 0U, 0U, NULL, d, s, 1U, ASYNC_DMA, 0U);
}
HAL_StatusTypeDef HAL_I2C_Mem_Write_DMA(I2C_HandleTypeDef *h, uint16_t a, uint16_t m, uint32_t ms,
                                        const uint8_t *d, uint32_t s)
{
    return async_start(h, a, m, ms, d, NULL, s, 0U, ASYNC_DMA, 1U);
}
HAL_StatusTypeDef HAL_I2C_Mem_Read_DMA(I2C_HandleTypeDef *h, uint16_t a, uint16_t m, uint32_t ms,
                                       uint8_t *d, uint32_t s)
{
    return async_start(h, a, m, ms, NULL, d, s, 1U, ASYNC_DMA, 1U);
}
__weak void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *h)
{
    UNUSED(h);
}
__weak void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef *h)
{
    UNUSED(h);
}
__weak void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *h)
{
    UNUSED(h);
}
__weak void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *h)
{
    UNUSED(h);
}
__weak void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *h)
{
    UNUSED(h);
}

__weak void HAL_I2C_MspInit(I2C_HandleTypeDef *handle)
{
    UNUSED(handle);
}
__weak void HAL_I2C_MspDeInit(I2C_HandleTypeDef *handle)
{
    UNUSED(handle);
}
