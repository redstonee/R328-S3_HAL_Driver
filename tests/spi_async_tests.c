/* Byte-clocked FIFO/DMA model. IRQ delivery is independent of hardware progress. */
#include "r328_hal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#define FIFO_DEPTH 64U
#define TEST_SIZE 131U
#define XCH (1UL << 31)
#define TC (1U << 12)
#define TX_DRQ (1UL << 24)
#define RX_DRQ (1U << 8)
static SPI_HandleTypeDef spi;
static DMA_HandleTypeDef tx_dma = {.Init = {0U, DMA_MEMORY_TO_PERIPH, DMA_REQUEST_SPI1}};
static DMA_HandleTypeDef rx_dma = {.Init = {1U, DMA_PERIPH_TO_MEMORY, DMA_REQUEST_SPI1}};
static uint8_t tx_fifo[FIFO_DEPTH], rx_fifo[FIFO_DEPTH], wire[4096];
static uint32_t tx_in, tx_out, rx_in, rx_out, wire_size, remaining, transmit_size;
static uint32_t dma_pos[2], tick, callbacks[4], cs_asserts, cs_deasserts, bursts;
static int stall_dma, stall_spi, stuck_fifo;

uint32_t HAL_GetTick(void)
{
    return tick;
}
static void callback(SPI_HandleTypeDef *h, uint32_t kind)
{
    assert(h == &spi && h->Lock == HAL_UNLOCKED && h->AsyncMode == 0U);
    assert(h->State == (kind == 3U ? HAL_SPI_STATE_RESET : HAL_SPI_STATE_READY));
    ++callbacks[kind];
}
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *h)
{
    callback(h, 0U);
}
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *h)
{
    callback(h, 1U);
}
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *h)
{
    callback(h, 2U);
}
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *h)
{
    callback(h, 3U);
}

static uint32_t fifo_status(void)
{
    return ((tx_in - tx_out) << 16) | (rx_in - rx_out);
}
static void put_tx(uint8_t value)
{
    assert(tx_in - tx_out < FIFO_DEPTH);
    tx_fifo[tx_in++ % FIFO_DEPTH] = value;
}
static uint8_t get_rx(void)
{
    assert(rx_in > rx_out);
    return rx_fifo[rx_out++ % FIFO_DEPTH];
}
static void dma_step(uint32_t channel)
{
    DMA_HandleTypeDef *h = channel == 0U ? &tx_dma : &rx_dma;
    if (stall_dma || DMA->CHANNEL[channel].EN == 0U)
    {
        return;
    }
    assert(DMA->CHANNEL[channel].MODE == 0xCU);
    assert(DMA->CHANNEL[channel].DESC == (uint32_t)(uintptr_t)h->Descriptor);
    assert(h->Descriptor[5] == 0xFFFFF800U);
    uint32_t *desc = h->Descriptor;
    uint32_t pos = dma_pos[channel];
    if (channel == 0U)
    {
        assert((desc[0] >> 16) == ((1U << 8) | h->Init.Request));
        assert(desc[2] == (uint32_t)(uintptr_t)&spi.Instance->TXD);
        if ((spi.Instance->FCR & TX_DRQ) == 0U || tx_in - tx_out > 32U)
        {
            return;
        }
        put_tx(((const uint8_t *)(uintptr_t)desc[1])[pos]);
    }
    else
    {
        assert((desc[0] & 0xFFFFU) == ((1U << 8) | h->Init.Request));
        assert(desc[1] == (uint32_t)(uintptr_t)&spi.Instance->RXD);
        if ((spi.Instance->FCR & RX_DRQ) == 0U || rx_in == rx_out)
        {
            return;
        }
        ((uint8_t *)(uintptr_t)desc[2])[pos] = get_rx();
    }
    DMA->CHANNEL[channel].LEFT = desc[3] - ++dma_pos[channel];
    if (dma_pos[channel] == desc[3])
    {
        DMA->CHANNEL[channel].EN = 0U;
        DMA->IRQ_PEND[0] |= 4U << (channel * 4U);
    }
}
static void hardware_step(void)
{
    dma_step(0U);
    SPI_TypeDef *s = spi.Instance;
    if (!stall_spi && (s->TCR & XCH) != 0U && remaining != 0U)
    {
        uint32_t sent = s->MBC - remaining;
        int needs_tx = sent < transmit_size;
        int receive = (s->TCR & (1U << 8)) == 0U;
        if ((!needs_tx || tx_in != tx_out) && (!receive || rx_in - rx_out < FIFO_DEPTH))
        {
            uint8_t byte = needs_tx ? tx_fifo[tx_out++ % FIFO_DEPTH] : 0xFFU;
            assert(wire_size < sizeof(wire));
            wire[wire_size++] = byte;
            if (receive)
            {
                rx_fifo[rx_in++ % FIFO_DEPTH] = byte;
            }
            if (--remaining == 0U)
            {
                s->TCR &= ~XCH;
                s->ISR |= TC;
            }
        }
    }
    dma_step(1U);
}
uint32_t R328_HAL_TestRead32(volatile uint32_t *reg)
{
    if (reg == &spi.Instance->FSR)
    {
        dma_step(0U); /* TX prefill progresses even while CPU IRQs are masked. */
        return fifo_status();
    }
    return *reg;
}
void R328_HAL_TestWrite32(volatile uint32_t *reg, uint32_t value)
{
    if (reg == &DMA->IRQ_PEND[0] || reg == &DMA->IRQ_PEND[1] || reg == &spi.Instance->ISR)
    {
        *reg &= ~value;
        return;
    }
    for (uint32_t channel = 0U; channel < 2U; ++channel)
    {
        if (reg == &DMA->CHANNEL[channel].EN && value == 1U)
        {
            dma_pos[channel] = 0U;
            DMA->CHANNEL[channel].LEFT =
                channel == 0U ? tx_dma.Descriptor[3] : rx_dma.Descriptor[3];
        }
    }
    if (reg == &spi.Instance->FCR && (value & 0x80008000U) != 0U)
    {
        tx_in = tx_out = rx_in = rx_out = 0U;
        *reg = stuck_fifo ? value : value & ~0x80008000U;
        return;
    }
    if (reg == &spi.Instance->TCR)
    {
        if ((*reg & 0x80U) != 0U && (value & 0x80U) == 0U)
        {
            ++cs_asserts;
        }
        if ((*reg & 0x80U) == 0U && (value & 0x80U) != 0U)
        {
            ++cs_deasserts;
        }
        if ((value & XCH) != 0U)
        {
            ++bursts;
            remaining = spi.Instance->MBC;
            transmit_size = spi.Instance->MTC;
            assert(spi.Instance->BCC == transmit_size);
            assert(transmit_size == 0U || tx_in > tx_out); /* Never clock empty TX. */
            if (spi.AsyncMode == 1U)
            {
                assert(remaining <= FIFO_DEPTH && tx_in - tx_out == transmit_size);
            }
        }
    }
    *reg = value;
}
uint8_t R328_HAL_TestRead8(volatile uint32_t *reg)
{
    assert(reg == &spi.Instance->RXD);
    return get_rx();
}
void R328_HAL_TestWrite8(volatile uint32_t *reg, uint8_t value)
{
    assert(reg == &spi.Instance->TXD);
    put_tx(value);
}
static void map(uintptr_t address, size_t size)
{
    assert(mmap((void *)address, size, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == (void *)address);
}
static void init(void)
{
    stall_spi = stall_dma = stuck_fifo = 0;
    assert(HAL_SPI_Init(&spi) == HAL_OK);
    assert(HAL_SPI_SetAsyncTimeout(&spi, 100U) == HAL_OK);
    spi.hdmatx = &tx_dma;
    spi.hdmarx = &rx_dma;
    wire_size = bursts = cs_asserts = cs_deasserts = 0U;
}
static void deliver_dma_irqs(void)
{
    HAL_DMA_IRQHandler(&rx_dma);
    HAL_DMA_IRQHandler(&tx_dma);
}
static void pump(void)
{
    for (uint32_t i = 0U; spi.State == HAL_SPI_STATE_BUSY && i < 10000U; ++i)
    {
        hardware_step();
        HAL_SPI_IRQHandler(&spi);
        deliver_dma_irqs();
        HAL_SPI_Process(&spi);
    }
    assert(spi.State != HAL_SPI_STATE_BUSY);
}
static void transfers(void)
{
    static const uint32_t sizes[] = {1U, 2U, 3U, 6U, 63U, 64U, 65U, TEST_SIZE, 257U};
    uint8_t *tx = (void *)(uintptr_t)0x28001U, *rx = (void *)(uintptr_t)0x29003U;
    for (uint32_t mode = 0U; mode < 2U; ++mode)
    {
        for (uint32_t kind = 0U; kind < 3U; ++kind)
        {
            for (uint32_t n = 0U; n < sizeof(sizes) / sizeof(sizes[0]); ++n)
            {
                uint32_t size = sizes[n], before = callbacks[kind];
                init();
                for (uint32_t i = 0U; i < size; ++i)
                {
                    tx[i] = (uint8_t)(i ^ 0xA5U);
                }
                memset(rx - 1U, 0x42, size + 2U);
                HAL_StatusTypeDef status;
                if (mode == 0U)
                {
                    status = kind == 0U   ? HAL_SPI_Transmit_IT(&spi, tx, size)
                             : kind == 1U ? HAL_SPI_Receive_IT(&spi, rx, size)
                                          : HAL_SPI_TransmitReceive_IT(&spi, tx, rx, size);
                }
                else
                {
                    status = kind == 0U   ? HAL_SPI_Transmit_DMA(&spi, tx, size)
                             : kind == 1U ? HAL_SPI_Receive_DMA(&spi, rx, size)
                                          : HAL_SPI_TransmitReceive_DMA(&spi, tx, rx, size);
                }
                assert(status == HAL_OK && callbacks[kind] == before);
                assert(HAL_SPI_Init(&spi) == HAL_BUSY && HAL_SPI_DeInit(&spi) == HAL_BUSY);
                assert(HAL_SPI_SetBaudRate(&spi, 10000000U) == HAL_BUSY);
                assert(HAL_SPI_Transmit_IT(&spi, tx, 1U) == HAL_BUSY);
                assert(HAL_SPI_Transmit(&spi, tx, 1U, 10U) == HAL_BUSY);
                pump();
                assert(callbacks[kind] == before + 1U && spi.ErrorCode == HAL_SPI_ERROR_NONE);
                assert(spi.XferCount == 0U && wire_size == size);
                assert(bursts == (mode == 0U ? (size + FIFO_DEPTH - 1U) / FIFO_DEPTH : 1U));
                assert(cs_asserts == 1U && cs_deasserts == 1U);
                assert(spi.Instance->IER == 0U && (spi.Instance->FCR & (TX_DRQ | RX_DRQ)) == 0U);
                for (uint32_t i = 0U; i < size; ++i)
                {
                    assert(wire[i] == (kind == 1U ? 0xFFU : tx[i]));
                    if (kind != 0U)
                    {
                        assert(rx[i] == wire[i]);
                    }
                }
                assert(rx[-1] == 0x42U && rx[size] == 0x42U);
                HAL_SPI_Process(&spi);
                HAL_SPI_IRQHandler(&spi);
                deliver_dma_irqs();
                assert(callbacks[kind] == before + 1U);
            }
        }
    }
}
static void ordering_and_errors(void)
{
    uint8_t *tx = (void *)(uintptr_t)0x28001U, *rx = (void *)(uintptr_t)0x29003U;
    for (uint32_t order = 0U; order < 2U; ++order)
    {
        init();
        uint32_t before = callbacks[2];
        assert(HAL_SPI_TransmitReceive_DMA(&spi, tx, rx, TEST_SIZE) == HAL_OK);
        for (uint32_t i = 0U; i < TEST_SIZE + 100U; ++i)
        {
            hardware_step();
        }
        if (order == 0U)
        {
            HAL_SPI_IRQHandler(&spi);
        }
        else
        {
            deliver_dma_irqs();
        }
        HAL_SPI_Process(&spi);
        assert(callbacks[2] == before && spi.State == HAL_SPI_STATE_BUSY);
        if (order == 0U)
        {
            deliver_dma_irqs();
        }
        else
        {
            HAL_SPI_IRQHandler(&spi);
        }
        HAL_SPI_Process(&spi);
        assert(callbacks[2] == before + 1U);
    }
    init();
    assert(HAL_SPI_TransmitReceive_DMA(&spi, tx, tx + 1, 3U) == HAL_ERROR);
    assert(HAL_SPI_Receive_DMA(&spi, rx, 0U) == HAL_ERROR);
    assert(HAL_SPI_Receive_DMA(&spi, rx, 0x1000000U) == HAL_ERROR);
    assert(HAL_SPI_TransmitReceive_IT(&spi, NULL, rx, 1U) == HAL_ERROR);
    /* RX started, then TX buffer rejected: stop RX before releasing ownership. */
    uint32_t before = callbacks[3];
    assert(HAL_SPI_TransmitReceive_DMA(&spi, (void *)(uintptr_t)0x50000U, rx, 3U) == HAL_ERROR);
    assert(spi.State == HAL_SPI_STATE_RESET && spi.Lock == HAL_UNLOCKED);
    assert(DMA->CHANNEL[1].EN == 0U && callbacks[3] == before);
    init();
    stall_dma = 1;
    assert(HAL_SPI_Transmit_DMA(&spi, tx, 6U) == HAL_TIMEOUT); /* Prefill never arrived. */
    assert(spi.State == HAL_SPI_STATE_RESET && DMA->CHANNEL[0].EN == 0U);
    init();
    stuck_fifo = 1;
    assert(HAL_SPI_Transmit_IT(&spi, tx, 1U) == HAL_TIMEOUT);
    assert(spi.State == HAL_SPI_STATE_RESET);
    init();
    assert(HAL_SPI_Receive_IT(&spi, rx, 1U) == HAL_OK);
    spi.Instance->ISR |= 1U << 8;
    HAL_SPI_IRQHandler(&spi);
    HAL_SPI_Process(&spi);
    assert(spi.ErrorCode == HAL_SPI_ERROR_FIFO && callbacks[3] == before + 1U);
    init();
    tick = UINT32_MAX - 2U;
    assert(HAL_SPI_SetAsyncTimeout(&spi, 5U) == HAL_OK);
    assert(HAL_SPI_Receive_DMA(&spi, rx, 6U) == HAL_OK);
    stall_spi = 1;
    tick = 2U;
    HAL_SPI_Process(&spi);
    assert(spi.ErrorCode == HAL_SPI_ERROR_TIMEOUT && callbacks[3] == before + 2U);
    init();
    assert(HAL_SPI_Receive_DMA(&spi, rx, 6U) == HAL_OK);
    DMA->STATUS = 1U << 1;
    assert(HAL_SPI_Abort(&spi) == HAL_TIMEOUT);
    assert(spi.State == HAL_SPI_STATE_BUSY && spi.Lock == HAL_LOCKED);
    assert(callbacks[3] == before + 2U);
    DMA->STATUS = 0U;
    HAL_SPI_Process(&spi);
    assert(spi.State == HAL_SPI_STATE_RESET && callbacks[3] == before + 3U);
    assert((spi.ErrorCode & (HAL_SPI_ERROR_ABORT | HAL_SPI_ERROR_DMA)) != 0U);
    init();
    assert(HAL_SPI_Receive_DMA(&spi, rx, 6U) == HAL_OK);
    /* A queue-end IRQ with untransferred bytes must not report success. */
    DMA->CHANNEL[1].LEFT = 3U;
    DMA->IRQ_PEND[0] |= 4U << 4;
    deliver_dma_irqs();
    HAL_SPI_Process(&spi);
    assert(spi.ErrorCode == HAL_SPI_ERROR_DMA && callbacks[3] == before + 4U);
    init();
    assert(HAL_SPI_Receive_DMA(&spi, rx, 6U) == HAL_OK);
    for (uint32_t i = 0U; i < 100U; ++i)
    {
        hardware_step();
    }
    HAL_SPI_IRQHandler(&spi);
    deliver_dma_irqs();
    rx_fifo[rx_in++ % FIFO_DEPTH] = 0U; /* Unexpected residual RX data. */
    HAL_SPI_Process(&spi);
    assert(spi.ErrorCode == HAL_SPI_ERROR_DMA && callbacks[3] == before + 5U);
    init();
    assert(HAL_SPI_Transmit_IT(&spi, tx, 1U) == HAL_OK);
    assert(HAL_SPI_Abort(&spi) == HAL_OK && callbacks[3] == before + 6U);
}
int main(void)
{
    map(0x03000000U, 0x10000U);
    map(0x03021000U, 0x2000U);
    map(0x05010000U, 0x2000U);
    map(0x00020000U, 0x20000U);
    R328_REG32(R328_GICD_BASE + 4U) = 5U;
    assert(HAL_GIC_Init() == HAL_OK);
    spi.Instance = SPI1;
    spi.Init = (SPI_InitTypeDef){.Mode = SPI_MODE_MASTER,
                                 .DataSize = SPI_DATASIZE_8BIT,
                                 .NSS = SPI_NSS_HARD_OUTPUT,
                                 .BaudRateHz = 2000000U};
    assert(HAL_DMA_Init(&tx_dma) == HAL_OK && HAL_DMA_Init(&rx_dma) == HAL_OK);
    transfers();
    ordering_and_errors();
    assert(HAL_SPI_DeInit(&spi) == HAL_OK);
    assert(HAL_DMA_DeInit(&rx_dma) == HAL_OK && HAL_DMA_DeInit(&tx_dma) == HAL_OK);
    /* The other SPI instance uses request 22 and its own GIC entry. */
    spi.Instance = SPI0;
    tx_dma.Init.Request = rx_dma.Init.Request = DMA_REQUEST_SPI0;
    assert(HAL_DMA_Init(&tx_dma) == HAL_OK && HAL_DMA_Init(&rx_dma) == HAL_OK);
    init();
    assert(HAL_SPI_Receive_DMA(&spi, (void *)(uintptr_t)0x29003U, 3U) == HAL_OK);
    pump();
    assert(HAL_SPI_DeInit(&spi) == HAL_OK);
    assert(HAL_DMA_DeInit(&rx_dma) == HAL_OK && HAL_DMA_DeInit(&tx_dma) == HAL_OK);
    puts("SPI IT/DMA byte FIFO, IRQ order, CS, guards, timeout and abort tests passed");
    return 0;
}
