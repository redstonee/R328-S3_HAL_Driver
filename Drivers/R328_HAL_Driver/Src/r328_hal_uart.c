#include "r328_hal_bus_private.h"

#include <string.h>

static uint32_t polling_timeout = HAL_POLLING_DEFAULT_TIMEOUT_MS;
void HAL_UART_SetPollingTimeout(uint32_t timeout)
{
    polling_timeout = timeout;
}

#define UART_FIFO_ENABLE (1U << 0)
#define UART_RX_FIFO_RESET (1U << 1)
#define UART_TX_FIFO_RESET (1U << 2)
#define UART_DMA_MODE (1U << 3)
#define UART_FIFO_CONFIG (UART_FIFO_ENABLE | UART_DMA_MODE)
#define UART_DMA_HANDSHAKE 0xE5U
#define UART_IER_RX (1U << 0)
#define UART_IER_TX (1U << 1)
#define UART_IER_LINE (1U << 2)
#define UART_IIR_ID_MASK 0xFU
#define UART_IIR_MODEM 0U
#define UART_IIR_NONE 1U
#define UART_IIR_RX_DATA 4U
#define UART_IIR_RX_LINE 6U
#define UART_IIR_BUSY 7U
#define UART_IIR_RX_TIMEOUT 12U
#define UART_LSR_DR (1U << 0)
#define UART_LSR_ERRORS                                                                            \
    (HAL_UART_ERROR_ORE | HAL_UART_ERROR_PE | HAL_UART_ERROR_FE | HAL_UART_ERROR_BI)
#define UART_LSR_TEMT (1U << 6)
#define UART_USR_BUSY (1U << 0)
#define UART_USR_TFNF (1U << 1)
#define UART_USR_TFE (1U << 2)
#define UART_LCR_DLAB (1U << 7)
#define UART_RX_FIFO_LEVEL_MASK 0x1FFU
#define UART_IRQ_BUDGET 256U
#define UART_DEFAULT_TIMEOUT_MS 100U
#define UART_DMA_MAX_SIZE 0x1FFFFFFU
#define UART_MODE_NONE 0U
#define UART_MODE_POLL 1U
#define UART_MODE_IT 2U
#define UART_MODE_DMA 3U

static UART_HandleTypeDef *owners[4];
static void async_service(UART_HandleTypeDef *h, int check_timeout);
static int valid_instance(const UART_TypeDef *instance)
{
    return instance == UART0 || instance == UART1 || instance == UART2 || instance == UART3;
}
static uint32_t instance_index(const UART_HandleTypeDef *h)
{
    return (uint32_t)(((uintptr_t)h->Instance - (uintptr_t)UART0) / 0x400U);
}
static int valid_handle(const UART_HandleTypeDef *h)
{
    return h != NULL && valid_instance(h->Instance);
}
static int available(const UART_HandleTypeDef *h)
{
    return owners[instance_index(h)] == NULL || owners[instance_index(h)] == h;
}
static IRQn_Type uart_irq_number(const UART_HandleTypeDef *h)
{
    return (IRQn_Type)((uint32_t)UART0_IRQn + instance_index(h));
}
static void update_state(UART_HandleTypeDef *h)
{
    h->State =
        h->Tx.Mode != UART_MODE_NONE
            ? (h->Rx.Mode != UART_MODE_NONE ? HAL_UART_STATE_BUSY_TX_RX : HAL_UART_STATE_BUSY_TX)
            : (h->Rx.Mode != UART_MODE_NONE ? HAL_UART_STATE_BUSY_RX : HAL_UART_STATE_READY);
    h->ErrorCode = h->Tx.ErrorCode | h->Rx.ErrorCode;
}
static uint32_t read_lsr(UART_HandleTypeDef *h)
{
    uint32_t irq_state = R328_IRQ_Save();
    uint32_t lsr = BUS_READ(h->Instance->LSR);
    /* LSR error bits clear on read, including reads made while checking TEMT.
     * Preserve them for the independently running receiver. */
    if (h->Rx.Mode != UART_MODE_NONE)
    {
        if ((lsr & UART_LSR_ERRORS) != 0U && h->RxErrorLineStatus == 0U)
        {
            h->RxErrorLineStatus = lsr;
            h->RxErrorFifoLevel = BUS_READ(h->Instance->RFL) & UART_RX_FIFO_LEVEL_MASK;
        }
        h->Rx.ErrorCode |= lsr & UART_LSR_ERRORS;
        h->ErrorCode = h->Tx.ErrorCode | h->Rx.ErrorCode;
    }
    R328_IRQ_Restore(irq_state);
    return lsr;
}
static void update_interrupts(UART_HandleTypeDef *h)
{
    uint32_t ier = 0U;
    /* Keep THRE enabled after the final FIFO write. The next empty-FIFO IRQ
     * releases TX even if the last character is still in the shift register.
     * DMA switches to this IRQ only after its completion interrupt. */
    if (h->Tx.ErrorCode == 0U &&
        (h->Tx.Mode == UART_MODE_IT || (h->Tx.Mode == UART_MODE_DMA && h->Tx.DmaDone != 0U)))
    {
        ier |= UART_IER_TX;
    }
    if (h->Rx.ErrorCode == 0U)
    {
        if (h->Rx.Mode == UART_MODE_IT && h->Rx.Count != 0U)
        {
            ier |= UART_IER_RX | UART_IER_LINE;
        }
        else if (h->Rx.Mode == UART_MODE_DMA)
        {
            /* RDA/character-timeout IRQs must not steal bytes from DMA. */
            ier |= UART_IER_LINE;
        }
    }
    BUS_WRITE(h->Instance->IER_DLH, ier);
    R328_DSB();
}
static HAL_StatusTypeDef wait_flag(UART_HandleTypeDef *handle, volatile uint32_t *reg,
                                   uint32_t mask, uint32_t start, uint32_t timeout)
{
    while (((reg == &handle->Instance->LSR ? read_lsr(handle) : BUS_READ(*reg)) & mask) == 0U)
    {
        if (bus_expired(start, timeout))
        {
            return HAL_TIMEOUT;
        }
    }
    return HAL_OK;
}
static HAL_StatusTypeDef poll_finish(UART_HandleTypeDef *h, UART_TransferTypeDef *transfer,
                                     HAL_StatusTypeDef status)
{
    uint32_t irq_state = R328_IRQ_Save();
    if (status == HAL_TIMEOUT)
    {
        transfer->ErrorCode |= HAL_UART_ERROR_TIMEOUT;
    }
    transfer->Mode = UART_MODE_NONE;
    update_state(h);
    HAL_Unlock(&h->Lock);
    R328_IRQ_Restore(irq_state);
    /* An opposite-direction IRQ may have completed while polling held Lock. */
    async_service(h, 0);
    return status;
}
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *handle)
{
    if (handle == NULL || !valid_instance(handle->Instance) || handle->Init.BaudRate == 0U ||
        (handle->Init.WordLength != UART_WORDLENGTH_7B &&
         handle->Init.WordLength != UART_WORDLENGTH_8B) ||
        (handle->Init.StopBits != UART_STOPBITS_1 && handle->Init.StopBits != UART_STOPBITS_2) ||
        (handle->Init.Parity != UART_PARITY_NONE && handle->Init.Parity != UART_PARITY_ODD &&
         handle->Init.Parity != UART_PARITY_EVEN))
    {
        return HAL_ERROR;
    }
    uint32_t pclk = HAL_RCC_GetPCLK2Freq();
    uint64_t denominator = (uint64_t)handle->Init.BaudRate * 16U;
    uint32_t divisor = (uint32_t)(((uint64_t)pclk + denominator / 2U) / denominator);
    if (pclk == 0U || divisor == 0U || divisor > 65535U)
    {
        return HAL_ERROR;
    }
    uint64_t target = denominator * divisor;
    uint64_t error = target > pclk ? target - pclk : pclk - target;
    if (error * 100U > target * 3U)
    {
        return HAL_ERROR;
    }
    if (!available(handle) || handle->Tx.Mode != UART_MODE_NONE ||
        handle->Rx.Mode != UART_MODE_NONE || HAL_TryLock(&handle->Lock) != HAL_OK)
    {
        return HAL_BUSY;
    }
    if (handle->State == HAL_UART_STATE_RESET)
    {
        HAL_UART_MspInit(handle);
    }
    (void)HAL_RCC_UART_EnableClock(handle->Instance);
    (void)HAL_RCC_UART_ForceReset(handle->Instance);
    UART_TypeDef *uart = handle->Instance;
    uint32_t remaining = HAL_REGISTER_POLL_LIMIT;
    while ((BUS_READ(uart->USR) & UART_USR_BUSY) != 0U)
    {
        if (--remaining == 0U)
        {
            handle->State = HAL_UART_STATE_RESET;
            HAL_Unlock(&handle->Lock);
            return HAL_TIMEOUT;
        }
    }
    BUS_WRITE(uart->LCR, 0U);
    BUS_WRITE(uart->IER_DLH, 0U);
    BUS_WRITE(uart->LCR, UART_LCR_DLAB);
    BUS_WRITE(uart->DATA, divisor & 0xFFU);
    BUS_WRITE(uart->IER_DLH, divisor >> 8);
    BUS_WRITE(uart->LCR, handle->Init.WordLength | handle->Init.StopBits | handle->Init.Parity);
    BUS_WRITE(uart->MCR, 0U);
    /* IIR shares this address: never read/modify/write FCR. */
    BUS_WRITE(uart->IIR_FCR, UART_FIFO_ENABLE | UART_RX_FIFO_RESET | UART_TX_FIFO_RESET);
    R328_DSB();
    BUS_WRITE(uart->HSK, UART_DMA_HANDSHAKE);
    BUS_WRITE(uart->HALT, 0U);
    handle->Tx = (UART_TransferTypeDef){0};
    handle->Rx = (UART_TransferTypeDef){0};
    handle->RxErrorLineStatus = handle->RxErrorFifoLevel = 0U;
    handle->XferTimeout = UART_DEFAULT_TIMEOUT_MS;
    update_state(handle);
    HAL_Unlock(&handle->Lock);
    return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_DeInit(UART_HandleTypeDef *handle)
{
    if (handle == NULL || !valid_instance(handle->Instance))
    {
        return HAL_ERROR;
    }
    if (!available(handle) || handle->Tx.Mode != UART_MODE_NONE ||
        handle->Rx.Mode != UART_MODE_NONE || HAL_TryLock(&handle->Lock) != HAL_OK)
    {
        return HAL_BUSY;
    }
    if (owners[instance_index(handle)] == handle)
    {
        (void)HAL_GIC_UnregisterIRQ(uart_irq_number(handle), handle);
        owners[instance_index(handle)] = NULL;
    }
    (void)HAL_RCC_UART_ForceReset(handle->Instance);
    HAL_UART_MspDeInit(handle);
    handle->State = HAL_UART_STATE_RESET;
    handle->ErrorCode = HAL_UART_ERROR_NONE;
    HAL_Unlock(&handle->Lock);
    return HAL_OK;
}
uint32_t HAL_UART_GetRxFifoLevel(const UART_HandleTypeDef *handle)
{
    if (!valid_handle(handle) || handle->State == HAL_UART_STATE_RESET)
    {
        return 0U;
    }
    return BUS_READ(handle->Instance->RFL) & UART_RX_FIFO_LEVEL_MASK;
}
static HAL_StatusTypeDef poll_begin(UART_HandleTypeDef *h, int transmit)
{
    if (!valid_handle(h) || h->State == HAL_UART_STATE_RESET)
    {
        return HAL_ERROR;
    }
    uint32_t irq_state = R328_IRQ_Save();
    UART_TransferTypeDef *transfer = transmit ? &h->Tx : &h->Rx;
    if (!available(h) || transfer->Mode != UART_MODE_NONE || HAL_TryLock(&h->Lock) != HAL_OK)
    {
        R328_IRQ_Restore(irq_state);
        return HAL_BUSY;
    }
    transfer->Mode = UART_MODE_POLL;
    transfer->ErrorCode = HAL_UART_ERROR_NONE;
    if (!transmit)
    {
        h->RxErrorLineStatus = h->RxErrorFifoLevel = 0U;
    }
    UART_TransferTypeDef *other = transmit ? &h->Rx : &h->Tx;
    if (other->Mode == UART_MODE_NONE)
    {
        other->ErrorCode = HAL_UART_ERROR_NONE;
    }
    update_state(h);
    R328_IRQ_Restore(irq_state);
    return HAL_OK;
}
static HAL_StatusTypeDef tx_begin(UART_HandleTypeDef *handle)
{
    return poll_begin(handle, 1);
}
static HAL_StatusTypeDef tx_write(UART_HandleTypeDef *handle, const uint8_t *data, uint32_t size,
                                  uint32_t start, uint32_t timeout)
{
    for (uint32_t i = 0U; i < size; ++i)
    {
        HAL_StatusTypeDef result =
            wait_flag(handle, &handle->Instance->USR, UART_USR_TFNF, start, timeout);
        if (result != HAL_OK)
        {
            return result;
        }
        BUS_WRITE(handle->Instance->DATA, data[i]);
    }
    return HAL_OK;
}
static HAL_StatusTypeDef tx_end(UART_HandleTypeDef *handle, HAL_StatusTypeDef result,
                                uint32_t start, uint32_t timeout)
{
    if (result == HAL_OK)
    {
        result = wait_flag(handle, &handle->Instance->LSR, UART_LSR_TEMT, start, timeout);
    }
    return poll_finish(handle, &handle->Tx, result);
}
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *handle, const uint8_t *data, uint32_t size)
{
    const uint32_t timeout = polling_timeout;
    if (data == NULL && size != 0U)
    {
        return HAL_ERROR;
    }
    HAL_StatusTypeDef result = tx_begin(handle);
    if (result != HAL_OK)
    {
        return result;
    }
    if (size == 0U)
    {
        return poll_finish(handle, &handle->Tx, HAL_OK);
    }
    uint32_t start = HAL_GetTick();
    result = tx_write(handle, data, size, start, timeout);
    return tx_end(handle, result, start, timeout);
}
HAL_StatusTypeDef HAL_UART_FlushTx(UART_HandleTypeDef *handle)
{
    const uint32_t timeout = polling_timeout;
    HAL_StatusTypeDef result = tx_begin(handle);
    return result == HAL_OK ? tx_end(handle, HAL_OK, HAL_GetTick(), timeout) : result;
}
static uint32_t decimal(uint8_t buffer[10], uint32_t value)
{
    uint32_t offset = 10U;
    do
    {
        buffer[--offset] = (uint8_t)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);
    return offset;
}
static const char hex_digits[] = "0123456789ABCDEF";
HAL_StatusTypeDef HAL_UART_PrintString(UART_HandleTypeDef *handle, const char *text)
{
    return text == NULL ? HAL_ERROR
                        : HAL_UART_Transmit(handle, (const uint8_t *)text, strlen(text));
}
HAL_StatusTypeDef HAL_UART_PrintU32(UART_HandleTypeDef *handle, uint32_t value)
{
    uint8_t text[10];
    uint32_t offset = decimal(text, value);
    return HAL_UART_Transmit(handle, &text[offset], sizeof(text) - offset);
}
HAL_StatusTypeDef HAL_UART_PrintHex(UART_HandleTypeDef *handle, uint32_t value)
{
    uint8_t text[8];
    for (uint32_t i = 0U; i < sizeof(text); ++i)
    {
        text[i] = (uint8_t)hex_digits[(value >> ((7U - i) * 4U)) & 0xFU];
    }
    return HAL_UART_Transmit(handle, text, sizeof(text));
}
HAL_StatusTypeDef HAL_UART_PrintValue(UART_HandleTypeDef *handle, const char *label, uint32_t value)
{
    const uint32_t timeout = polling_timeout;
    if (label == NULL)
    {
        return HAL_ERROR;
    }
    HAL_StatusTypeDef result = tx_begin(handle);
    if (result != HAL_OK)
    {
        return result;
    }
    uint32_t start = HAL_GetTick();
    uint8_t text[10];
    uint32_t offset = decimal(text, value);
    result = tx_write(handle, (const uint8_t *)label, strlen(label), start, timeout);
    if (result == HAL_OK)
    {
        result = tx_write(handle, &text[offset], sizeof(text) - offset, start, timeout);
    }
    if (result == HAL_OK)
    {
        result = tx_write(handle, (const uint8_t *)"\r\n", 2U, start, timeout);
    }
    return tx_end(handle, result, start, timeout);
}
HAL_StatusTypeDef HAL_UART_HexDump(UART_HandleTypeDef *handle, const uint8_t *data, uint32_t size)
{
    const uint32_t timeout = polling_timeout;
    if (data == NULL && size != 0U)
    {
        return HAL_ERROR;
    }
    HAL_StatusTypeDef result = tx_begin(handle);
    if (result != HAL_OK)
    {
        return result;
    }
    uint32_t start = HAL_GetTick();
    while (size != 0U && result == HAL_OK)
    {
        uint8_t text[48]; /* Sixteen input bytes, bounded stack use. */
        uint32_t count = size < 16U ? size : 16U;
        for (uint32_t i = 0U; i < count; ++i)
        {
            text[i * 3U] = (uint8_t)hex_digits[data[i] >> 4];
            text[i * 3U + 1U] = (uint8_t)hex_digits[data[i] & 0xFU];
            text[i * 3U + 2U] = ' ';
        }
        result = tx_write(handle, text, count * 3U, start, timeout);
        data += count;
        size -= count;
    }
    if (result == HAL_OK)
    {
        result = tx_write(handle, (const uint8_t *)"\r\n", 2U, start, timeout);
    }
    return tx_end(handle, result, start, timeout);
}
HAL_StatusTypeDef HAL_UART_Receive(UART_HandleTypeDef *handle, uint8_t *data, uint32_t size)
{
    const uint32_t timeout = polling_timeout;
    if (data == NULL && size != 0U)
    {
        return HAL_ERROR;
    }
    HAL_StatusTypeDef status = poll_begin(handle, 0);
    if (status != HAL_OK)
    {
        return status;
    }
    uint32_t start = HAL_GetTick();
    for (uint32_t index = 0U; index < size; ++index)
    {
        for (;;)
        {
            uint32_t lsr = read_lsr(handle);
            if (handle->Rx.ErrorCode != HAL_UART_ERROR_NONE)
            {
                if ((lsr & UART_LSR_DR) != 0U)
                {
                    (void)BUS_READ(handle->Instance->DATA);
                }
                return poll_finish(handle, &handle->Rx, HAL_ERROR);
            }
            if ((lsr & UART_LSR_DR) != 0U)
            {
                break;
            }
            if (bus_expired(start, timeout))
            {
                return poll_finish(handle, &handle->Rx, HAL_TIMEOUT);
            }
        }
        data[index] = (uint8_t)BUS_READ(handle->Instance->DATA);
    }
    return poll_finish(handle, &handle->Rx, HAL_OK);
}
__weak void HAL_UART_MspInit(UART_HandleTypeDef *handle)
{
    UNUSED(handle);
}
__weak void HAL_UART_MspDeInit(UART_HandleTypeDef *handle)
{
    UNUSED(handle);
}

static void uart_irq(void *context)
{
    HAL_UART_IRQHandler(context);
}
static void uart_dma_complete(DMA_HandleTypeDef *dma)
{
    UART_HandleTypeDef *h = dma->Parent;
    if (!valid_handle(h))
    {
        return;
    }
    UART_TransferTypeDef *transfer = dma == h->hdmatx && h->Tx.Mode == UART_MODE_DMA   ? &h->Tx
                                     : dma == h->hdmarx && h->Rx.Mode == UART_MODE_DMA ? &h->Rx
                                                                                       : NULL;
    if (transfer != NULL && transfer->Mode == UART_MODE_DMA)
    {
        transfer->Count = BUS_READ(DMA->CHANNEL[dma->Init.Channel].LEFT);
        if (dma->ErrorCode != HAL_DMA_ERROR_NONE)
        {
            transfer->ErrorCode |= HAL_UART_ERROR_DMA;
        }
        transfer->DmaDone = 1U;
        /* A completed DMA receive can still have a pending line-status IRQ.
         * Capture it before releasing the buffer; do not poll LSR while idle. */
        if (transfer == &h->Rx)
        {
            (void)read_lsr(h);
        }
        async_service(h, 0);
    }
}
static HAL_StatusTypeDef async_start(UART_HandleTypeDef *h, const uint8_t *tx, uint8_t *rx,
                                     uint32_t size, int transmit, uint32_t mode)
{
    if (!valid_handle(h) || h->State == HAL_UART_STATE_RESET || size == 0U ||
        (transmit ? tx == NULL : rx == NULL) || (mode == UART_MODE_DMA && size > UART_DMA_MAX_SIZE))
    {
        return HAL_ERROR;
    }
    uint32_t irq_state = R328_IRQ_Save();
    UART_TransferTypeDef *transfer = transmit ? &h->Tx : &h->Rx;
    UART_TransferTypeDef *other = transmit ? &h->Rx : &h->Tx;
    DMA_HandleTypeDef *dma = transmit ? h->hdmatx : h->hdmarx;
    DMA_HandleTypeDef *other_dma = transmit ? h->hdmarx : h->hdmatx;
    if (!available(h) || transfer->Mode != UART_MODE_NONE || HAL_TryLock(&h->Lock) != HAL_OK)
    {
        R328_IRQ_Restore(irq_state);
        return HAL_BUSY;
    }
    HAL_StatusTypeDef result = HAL_OK;
    if (mode == UART_MODE_DMA &&
        (dma == NULL || dma->Init.Channel >= 10U || dma->State != HAL_DMA_STATE_READY ||
         dma->Init.Direction != (transmit ? DMA_MEMORY_TO_PERIPH : DMA_PERIPH_TO_MEMORY) ||
         dma->Init.Request != DMA_REQUEST_UART0 + instance_index(h) ||
         (other->Mode == UART_MODE_DMA && other_dma != NULL &&
          other_dma->Init.Channel == dma->Init.Channel)))
    {
        result = HAL_ERROR;
    }
    if (result == HAL_OK)
    {
        result = HAL_GIC_RegisterIRQ(uart_irq_number(h), uart_irq, h, 0xA0U);
        if (result == HAL_OK)
        {
            result = HAL_GIC_EnableIRQ(uart_irq_number(h));
        }
        if (result == HAL_OK)
        {
            owners[instance_index(h)] = h;
        }
    }
    if (result == HAL_OK)
    {
        *transfer = (UART_TransferTypeDef){
            .Size = size, .Start = HAL_GetTick(), .Count = size, .Mode = mode};
        if (other->Mode == UART_MODE_NONE)
        {
            other->ErrorCode = HAL_UART_ERROR_NONE;
        }
        if (transmit)
        {
            h->pTxBuffPtr = tx;
        }
        else
        {
            h->pRxBuffPtr = rx;
            h->RxErrorLineStatus = h->RxErrorFifoLevel = 0U;
        }
        /* DMA1: RX threshold = one byte; TX requests from empty until full.
         * No FIFO reset here: the other direction can already be running and
         * RX may contain the beginning of the next fixed-size transfer.
         * IIR/FCR share an address, so never read/modify/write this register. */
        BUS_WRITE(h->Instance->IIR_FCR, UART_FIFO_CONFIG);
        if (mode == UART_MODE_DMA)
        {
            dma->Parent = h;
            dma->XferCpltCallback = uart_dma_complete;
            result =
                HAL_DMA_Start_IT(dma, transmit ? (uintptr_t)tx : (uintptr_t)&h->Instance->DATA,
                                 transmit ? (uintptr_t)&h->Instance->DATA : (uintptr_t)rx, size);
            if (result != HAL_OK)
            {
                transfer->Mode = UART_MODE_NONE;
            }
        }
        update_state(h);
        update_interrupts(h);
    }
    HAL_Unlock(&h->Lock);
    R328_IRQ_Restore(irq_state);
    return result;
}
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *h, const uint8_t *data, uint32_t size)
{
    return async_start(h, data, NULL, size, 1, UART_MODE_IT);
}
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *h, uint8_t *data, uint32_t size)
{
    return async_start(h, NULL, data, size, 0, UART_MODE_IT);
}
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *h, const uint8_t *data, uint32_t size)
{
    return async_start(h, data, NULL, size, 1, UART_MODE_DMA);
}
HAL_StatusTypeDef HAL_UART_Receive_DMA(UART_HandleTypeDef *h, uint8_t *data, uint32_t size)
{
    return async_start(h, NULL, data, size, 0, UART_MODE_DMA);
}
HAL_StatusTypeDef HAL_UART_SetAsyncTimeout(UART_HandleTypeDef *h, uint32_t timeout)
{
    if (!valid_handle(h) || h->State == HAL_UART_STATE_RESET)
    {
        return HAL_ERROR;
    }
    uint32_t irq_state = R328_IRQ_Save();
    if (!available(h) || h->Tx.Mode != UART_MODE_NONE || h->Rx.Mode != UART_MODE_NONE ||
        HAL_TryLock(&h->Lock) != HAL_OK)
    {
        R328_IRQ_Restore(irq_state);
        return HAL_BUSY;
    }
    h->XferTimeout = timeout;
    HAL_Unlock(&h->Lock);
    R328_IRQ_Restore(irq_state);
    return HAL_OK;
}
void HAL_UART_IRQHandler(UART_HandleTypeDef *h)
{
    if (!valid_handle(h) || owners[instance_index(h)] != h)
    {
        return;
    }
    /* Reading IIR acknowledges non-programmable THRE. Busy/modem events have
     * separate clear-on-read registers; no FIFO reads for DMA RX. */
    uint32_t cause = BUS_READ(h->Instance->IIR_FCR) & UART_IIR_ID_MASK;
    if (cause == UART_IIR_NONE)
    {
        return;
    }
    if (cause == UART_IIR_BUSY)
    {
        (void)BUS_READ(h->Instance->USR);
    }
    else if (cause == UART_IIR_MODEM)
    {
        (void)BUS_READ(h->Instance->MSR);
    }
    int receive_event =
        cause == UART_IIR_RX_DATA || cause == UART_IIR_RX_LINE || cause == UART_IIR_RX_TIMEOUT;
    if (cause == UART_IIR_RX_LINE)
    {
        (void)read_lsr(h);
    }
    if (receive_event && h->Rx.Mode == UART_MODE_IT && h->Rx.ErrorCode == HAL_UART_ERROR_NONE)
    {
        for (uint32_t i = 0U; i < UART_IRQ_BUDGET && h->Rx.Count != 0U; ++i)
        {
            /* Only inspect receive status for a queued byte. In particular,
             * do not keep sampling clear-on-read LSR while the next character
             * is still shifting in and the FIFO is empty. */
            if (BUS_READ(h->Instance->RFL) == 0U)
            {
                break;
            }
            uint32_t lsr = read_lsr(h);
            if (h->Rx.ErrorCode != HAL_UART_ERROR_NONE || (lsr & UART_LSR_DR) == 0U)
            {
                break;
            }
            h->pRxBuffPtr[h->Rx.Size - h->Rx.Count] = (uint8_t)BUS_READ(h->Instance->DATA);
            --h->Rx.Count;
        }
    }
    if (h->Tx.Mode == UART_MODE_IT && h->Tx.ErrorCode == HAL_UART_ERROR_NONE)
    {
        for (uint32_t i = 0U; i < UART_IRQ_BUDGET && h->Tx.Count != 0U; ++i)
        {
            if ((BUS_READ(h->Instance->USR) & UART_USR_TFNF) == 0U)
            {
                break;
            }
            BUS_WRITE(h->Instance->DATA, h->pTxBuffPtr[h->Tx.Size - h->Tx.Count]);
            --h->Tx.Count;
        }
    }
    async_service(h, 0);
    update_interrupts(h);
}
/* Called with IRQs masked and the handle locked. Retain ownership if a DMA
 * channel has not stopped: the caller must not reuse its buffer yet. */
static int finish_async(UART_HandleTypeDef *h, int transmit)
{
    UART_TransferTypeDef *transfer = transmit ? &h->Tx : &h->Rx;
    DMA_HandleTypeDef *dma = transmit ? h->hdmatx : h->hdmarx;
    if (transfer->Mode == UART_MODE_DMA)
    {
        if (HAL_DMA_Abort(dma) != HAL_OK)
        {
            transfer->ErrorCode |= HAL_UART_ERROR_DMA;
            return 0;
        }
        transfer->Count = BUS_READ(DMA->CHANNEL[dma->Init.Channel].LEFT);
    }
    if (transfer->ErrorCode != HAL_UART_ERROR_NONE)
    {
        BUS_WRITE(h->Instance->IIR_FCR,
                  UART_FIFO_CONFIG | (transmit ? UART_TX_FIFO_RESET : UART_RX_FIFO_RESET));
    }
    transfer->Mode = UART_MODE_NONE;
    R328_DSB();
    return 1;
}
static void async_service(UART_HandleTypeDef *h, int check_timeout)
{
    if (!valid_handle(h) || h->State == HAL_UART_STATE_RESET ||
        (h->Tx.Mode < UART_MODE_IT && h->Rx.Mode < UART_MODE_IT))
    {
        return;
    }
    uint32_t irq_state = R328_IRQ_Save();
    if (HAL_TryLock(&h->Lock) != HAL_OK)
    {
        R328_IRQ_Restore(irq_state);
        return;
    }
    uint32_t completed = 0U, failed = 0U;
    for (uint32_t direction = 0U; direction < 2U; ++direction)
    {
        int transmit = direction == 0U;
        UART_TransferTypeDef *transfer = transmit ? &h->Tx : &h->Rx;
        DMA_HandleTypeDef *dma = transmit ? h->hdmatx : h->hdmarx;
        if (transfer->Mode < UART_MODE_IT)
        {
            continue;
        }
        if (transfer->Mode == UART_MODE_DMA)
        {
            transfer->Count = BUS_READ(DMA->CHANNEL[dma->Init.Channel].LEFT);
        }
        int done =
            transfer->Count == 0U && (transfer->Mode == UART_MODE_IT || transfer->DmaDone != 0U);
        /* USR is non-destructive: no LSR reads while waiting for TX empty.
         * FIFO empty is sufficient to accept another transfer. */
        if (done && transmit)
        {
            done = (BUS_READ(h->Instance->USR) & UART_USR_TFE) != 0U;
        }
        if (check_timeout && !done && bus_expired(transfer->Start, h->XferTimeout))
        {
            transfer->ErrorCode |= HAL_UART_ERROR_TIMEOUT;
        }
        if ((done || transfer->ErrorCode != HAL_UART_ERROR_NONE) && finish_async(h, transmit))
        {
            if (transfer->ErrorCode == HAL_UART_ERROR_NONE)
            {
                completed |= 1U << direction;
            }
            else
            {
                failed = 1U;
            }
        }
    }
    update_state(h);
    update_interrupts(h);
    HAL_Unlock(&h->Lock);
    R328_IRQ_Restore(irq_state);
    if (failed != 0U)
    {
        HAL_UART_ErrorCallback(h);
    }
    if ((completed & 1U) != 0U)
    {
        HAL_UART_TxCpltCallback(h);
    }
    if ((completed & 2U) != 0U)
    {
        HAL_UART_RxCpltCallback(h);
    }
}
void HAL_UART_Process(UART_HandleTypeDef *h)
{
    async_service(h, 1);
}
static HAL_StatusTypeDef abort_directions(UART_HandleTypeDef *h, uint32_t directions)
{
    if (!valid_handle(h) || h->State == HAL_UART_STATE_RESET)
    {
        return HAL_ERROR;
    }
    uint32_t irq_state = R328_IRQ_Save();
    if (!available(h) || HAL_TryLock(&h->Lock) != HAL_OK)
    {
        R328_IRQ_Restore(irq_state);
        return HAL_BUSY;
    }
    HAL_StatusTypeDef result = HAL_OK;
    uint32_t notify = 0U;
    for (uint32_t direction = 0U; direction < 2U; ++direction)
    {
        UART_TransferTypeDef *transfer = direction == 0U ? &h->Tx : &h->Rx;
        if ((directions & (1U << direction)) != 0U && transfer->Mode >= UART_MODE_IT)
        {
            transfer->ErrorCode |= HAL_UART_ERROR_ABORT;
            if (finish_async(h, direction == 0U))
            {
                notify = 1U;
            }
            else
            {
                result = HAL_TIMEOUT;
            }
        }
    }
    update_state(h);
    update_interrupts(h);
    HAL_Unlock(&h->Lock);
    R328_IRQ_Restore(irq_state);
    if (notify != 0U)
    {
        HAL_UART_ErrorCallback(h);
    }
    return result;
}
HAL_StatusTypeDef HAL_UART_Abort(UART_HandleTypeDef *h)
{
    return abort_directions(h, 3U);
}
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *h)
{
    return abort_directions(h, 1U);
}
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *h)
{
    return abort_directions(h, 2U);
}
__weak void HAL_UART_TxCpltCallback(UART_HandleTypeDef *h)
{
    UNUSED(h);
}
__weak void HAL_UART_RxCpltCallback(UART_HandleTypeDef *h)
{
    UNUSED(h);
}
__weak void HAL_UART_ErrorCallback(UART_HandleTypeDef *h)
{
    UNUSED(h);
}
