/* UART FIFO/shift register and byte-request DMA model, independent IRQ delivery. */
#include "r328_hal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#define FIFO_DEPTH 256U
#define TEST_BYTES 601U
#define IER_RX 1U
#define IER_TX 2U
#define IER_LINE 4U
#define LSR_DR 1U
#define LSR_THRE (1U << 5)
#define LSR_TEMT (1U << 6)
#define LSR_ERRORS 0x1EU
#define LCR_DLAB (1U << 7)
static UART_HandleTypeDef uart;
static DMA_HandleTypeDef tx_dma = {.Init = {0U, DMA_MEMORY_TO_PERIPH, DMA_REQUEST_UART0}};
static DMA_HandleTypeDef rx_dma = {.Init = {1U, DMA_PERIPH_TO_MEMORY, DMA_REQUEST_UART0}};
static uint8_t tx_fifo[FIFO_DEPTH], rx_fifo[FIFO_DEPTH], wire[4096];
static uint32_t tx_in, tx_out, rx_in, rx_out, wire_size, shift_busy, line_errors, fifo_config;
static uint32_t dma_pos[2], callbacks[3], tick;
static int in_irq, callback_rearm, polling_clock;
static void hardware_step(void);
static uint8_t rearm_byte;

uint32_t HAL_GetTick(void)
{
    return tick;
}
static void callback(UART_HandleTypeDef *h, uint32_t kind)
{
    assert(h == &uart && h->Lock == HAL_UNLOCKED && !in_irq);
    ++callbacks[kind];
}
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *h)
{
    assert(h->Tx.Mode == 0U && h->Tx.Count == 0U);
    callback(h, 0U);
}
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *h)
{
    assert(h->Rx.Mode == 0U && h->Rx.Count == 0U);
    callback(h, 1U);
    if (callback_rearm)
    {
        callback_rearm = 0;
        assert(HAL_UART_Receive_IT(h, &rearm_byte, 1U) == HAL_OK);
    }
}
void HAL_UART_ErrorCallback(UART_HandleTypeDef *h)
{
    assert(h->ErrorCode != HAL_UART_ERROR_NONE);
    callback(h, 2U);
}
static uint32_t line_status(void)
{
    return line_errors | (rx_in != rx_out ? LSR_DR : 0U) |
           (tx_in == tx_out ? LSR_THRE | (shift_busy ? 0U : LSR_TEMT) : 0U);
}
static void put_tx(uint8_t value)
{
    assert(tx_in - tx_out < FIFO_DEPTH);
    tx_fifo[tx_in++ % FIFO_DEPTH] = value;
}
static uint8_t get_rx(void)
{
    assert(rx_in != rx_out);
    return rx_fifo[rx_out++ % FIFO_DEPTH];
}
static void put_rx(uint8_t value)
{
    assert(rx_in - rx_out < FIFO_DEPTH);
    rx_fifo[rx_in++ % FIFO_DEPTH] = value;
}
uint32_t R328_HAL_TestRead32(volatile uint32_t *reg)
{
    UART_TypeDef *u = uart.Instance;
    if (reg == &u->DATA && (u->LCR & LCR_DLAB) == 0U)
    {
        return get_rx();
    }
    if (reg == &u->LSR)
    {
        if (polling_clock)
        {
            hardware_step();
        }
        uint32_t status = line_status();
        line_errors = 0U; /* LSR errors are clear on read, including TEMT polls. */
        return status;
    }
    if (reg == &u->USR)
    {
        return (tx_in - tx_out < FIFO_DEPTH ? 2U : 0U) | (shift_busy ? 1U : 0U);
    }
    if (reg == &u->RFL)
    {
        return rx_in - rx_out;
    }
    if (reg == &u->IIR_FCR)
    {
        if (line_errors && (u->IER_DLH & IER_LINE))
        {
            return 6U;
        }
        if (rx_in != rx_out && (u->IER_DLH & IER_RX))
        {
            return 4U;
        }
        return tx_in == tx_out && (u->IER_DLH & IER_TX) ? 2U : 1U;
    }
    return *reg;
}
void R328_HAL_TestWrite32(volatile uint32_t *reg, uint32_t value)
{
    UART_TypeDef *u = uart.Instance;
    if (reg == &u->DATA && (u->LCR & LCR_DLAB) == 0U)
    {
        put_tx((uint8_t)value);
        return;
    }
    if (reg == &u->IIR_FCR)
    {
        assert((value & ~0xFU) == 0U); /* Detect accidental IIR read/modify/write. */
        if (value & 2U)
        {
            rx_in = rx_out = 0U;
            line_errors = 0U;
        }
        if (value & 4U)
        {
            tx_in = tx_out = 0U;
        }
        fifo_config = value & ~6U;
        return;
    }
    if (reg == &DMA->IRQ_PEND[0] || reg == &DMA->IRQ_PEND[1])
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
    *reg = value;
}
static void dma_step(uint32_t channel)
{
    if (DMA->CHANNEL[channel].EN == 0U)
    {
        return;
    }
    DMA_HandleTypeDef *dma = channel == 0U ? &tx_dma : &rx_dma;
    uint32_t *desc = dma->Descriptor;
    assert(DMA->CHANNEL[channel].MODE == 0xCU && uart.Instance->HSK == 0xE5U);
    assert(fifo_config == 9U && (uart.Instance->HALT & 0xC0U) == 0U);
    assert(DMA->CHANNEL[channel].DESC == (uint32_t)(uintptr_t)desc);
    assert(desc[5] == 0xFFFFF800U &&
           dma->Init.Request ==
               DMA_REQUEST_UART0 + ((uintptr_t)uart.Instance - (uintptr_t)UART0) / 0x400U);
    uint32_t pos = dma_pos[channel];
    if (channel == 0U)
    {
        assert((desc[0] >> 16) == ((1U << 8) | dma->Init.Request));
        assert(desc[2] == (uint32_t)(uintptr_t)&uart.Instance->DATA);
        if (tx_in - tx_out == FIFO_DEPTH)
        {
            return;
        }
        put_tx(((const uint8_t *)(uintptr_t)desc[1])[pos]);
    }
    else
    {
        assert((desc[0] & 0xFFFFU) == ((1U << 8) | dma->Init.Request));
        assert(desc[1] == (uint32_t)(uintptr_t)&uart.Instance->DATA);
        if (rx_in == rx_out)
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
    shift_busy = 0U;
    if (tx_in != tx_out)
    {
        assert(wire_size < sizeof(wire));
        wire[wire_size++] = tx_fifo[tx_out++ % FIFO_DEPTH];
        shift_busy = 1U; /* TEMT stays low for one more character boundary. */
    }
    dma_step(1U);
}
static void interrupts(void)
{
    in_irq = 1;
    uint32_t cause = R328_HAL_TestRead32(&uart.Instance->IIR_FCR);
    if (cause != 1U)
    {
        R328_REG32(R328_GICC_BASE + 0xCU) =
            (uint32_t)UART0_IRQn +
            (uint32_t)(((uintptr_t)uart.Instance - (uintptr_t)UART0) / 0x400U);
        HAL_GIC_IRQHandler();
    }
    HAL_DMA_IRQHandler(&tx_dma);
    HAL_DMA_IRQHandler(&rx_dma);
    in_irq = 0;
}
static void init(void)
{
    assert(HAL_UART_Init(&uart) == HAL_OK);
    assert(HAL_UART_SetAsyncTimeout(&uart, 100U) == HAL_OK);
    uart.hdmatx = &tx_dma;
    uart.hdmarx = &rx_dma;
    wire_size = shift_busy = 0U;
}
static void pump(void)
{
    for (uint32_t i = 0U; uart.State != HAL_UART_STATE_READY && i < 10000U; ++i)
    {
        hardware_step();
        interrupts();
        HAL_UART_Process(&uart);
    }
    assert(uart.State == HAL_UART_STATE_READY);
}
static void transfers(void)
{
    const uint32_t sizes[] = {1U, 2U, 3U, 63U, 64U, 65U, 255U, 256U, 257U, TEST_BYTES};
    uint8_t *tx = (void *)(uintptr_t)0x28001U, *rx = (void *)(uintptr_t)0x29003U;
    for (uint32_t tx_mode = 0U; tx_mode < 2U; ++tx_mode)
    {
        for (uint32_t rx_mode = 0U; rx_mode < 2U; ++rx_mode)
        {
            for (uint32_t n = 0U; n < sizeof(sizes) / sizeof(sizes[0]); ++n)
            {
                uint32_t size = sizes[n], tx_before = callbacks[0], rx_before = callbacks[1];
                init();
                for (uint32_t i = 0U; i < size; ++i)
                {
                    tx[i] = (uint8_t)(i ^ 0xA5U);
                }
                memset(rx - 1U, 0x42, size + 2U);
                assert((rx_mode == 0U ? HAL_UART_Receive_IT(&uart, rx, size)
                                      : HAL_UART_Receive_DMA(&uart, rx, size)) == HAL_OK);
                assert((tx_mode == 0U ? HAL_UART_Transmit_IT(&uart, tx, size)
                                      : HAL_UART_Transmit_DMA(&uart, tx, size)) == HAL_OK);
                assert(uart.State == HAL_UART_STATE_BUSY_TX_RX && uart.Lock == HAL_UNLOCKED);
                assert(HAL_UART_Init(&uart) == HAL_BUSY && HAL_UART_DeInit(&uart) == HAL_BUSY);
                assert(HAL_UART_SetAsyncTimeout(&uart, 5U) == HAL_BUSY);
                assert(HAL_UART_Receive_IT(&uart, rx, 1U) == HAL_BUSY);
                HAL_UART_SetPollingTimeout(0U);
                assert(HAL_UART_Transmit(&uart, tx, 1U) == HAL_BUSY);
                /* Only IRQs/DMA move FIFO bytes; Process cannot poll the FIFO. */
                HAL_UART_Process(&uart);
                assert(wire_size == 0U && uart.Rx.Count == size && uart.Tx.Count == size);
                for (uint32_t i = 0U; i < size; ++i)
                {
                    put_rx((uint8_t)(i ^ 0x3CU));
                    hardware_step();
                    interrupts();
                    HAL_UART_Process(&uart);
                }
                pump();
                assert(callbacks[0] == tx_before + 1U && callbacks[1] == rx_before + 1U);
                assert(uart.ErrorCode == 0U && wire_size == size);
                assert(memcmp(tx, wire, size) == 0);
                for (uint32_t i = 0U; i < size; ++i)
                {
                    assert(rx[i] == (uint8_t)(i ^ 0x3CU));
                }
                assert(rx[-1] == 0x42U && rx[size] == 0x42U && uart.Instance->IER_DLH == 0U);
                HAL_UART_Process(&uart);
                assert(callbacks[0] == tx_before + 1U && callbacks[1] == rx_before + 1U);
            }
        }
    }
}
static void completion_order(void)
{
    uint8_t *tx = (void *)(uintptr_t)0x28001U, *rx = (void *)(uintptr_t)0x29003U;
    for (uint32_t mode = 0U; mode < 2U; ++mode)
    {
        init();
        uint32_t before = callbacks[0];
        assert((mode == 0U ? HAL_UART_Transmit_IT(&uart, tx, 1U)
                           : HAL_UART_Transmit_DMA(&uart, tx, 1U)) == HAL_OK);
        interrupts();
        if (mode != 0U)
        {
            dma_step(0U);
            interrupts();
        }
        HAL_UART_Process(&uart);
        assert(callbacks[0] == before); /* TX FIFO still contains one byte. */
        hardware_step();
        HAL_UART_Process(&uart);
        assert(callbacks[0] == before && uart.State == HAL_UART_STATE_BUSY_TX); /* Shift busy. */
        hardware_step();
        HAL_UART_Process(&uart);
        assert(callbacks[0] == before + 1U);

        /* A queued next frame must survive completion and rearming. */
        before = callbacks[1];
        assert((mode == 0U ? HAL_UART_Receive_IT(&uart, rx, 1U)
                           : HAL_UART_Receive_DMA(&uart, rx, 1U)) == HAL_OK);
        put_rx(0x81U);
        put_rx(0x92U);
        hardware_step();
        interrupts();
        if (mode != 0U)
        {
            /* No RX completion before the DMA IRQ, even though LEFT is zero. */
            assert(callbacks[1] == before);
        }
        callback_rearm = 1;
        HAL_UART_Process(&uart);
        assert(rx[0] == 0x81U && callbacks[1] == before + 1U);
        interrupts();
        HAL_UART_Process(&uart);
        assert(rearm_byte == 0x92U && callbacks[1] == before + 2U);
    }
    init();
    uint32_t before = callbacks[1];
    assert(HAL_UART_Receive_DMA(&uart, rx, 1U) == HAL_OK);
    put_rx(0xA5U);
    hardware_step();
    HAL_UART_Process(&uart);
    assert(callbacks[1] == before && uart.State == HAL_UART_STATE_BUSY_RX);
    interrupts();
    HAL_UART_Process(&uart);
    assert(callbacks[1] == before + 1U);
    /* A direction may be the only DMA user even if both link fields name the
     * same handle. Match the active direction, not just the first pointer. */
    init();
    uart.hdmatx = uart.hdmarx;
    before = callbacks[1];
    assert(HAL_UART_Receive_DMA(&uart, rx, 1U) == HAL_OK);
    put_rx(0x69U);
    hardware_step();
    interrupts();
    HAL_UART_Process(&uart);
    assert(callbacks[1] == before + 1U && rx[0] == 0x69U);
    uart.hdmatx = &tx_dma;
}
static void guards_and_errors(void)
{
    uint8_t *tx = (void *)(uintptr_t)0x28001U, *rx = (void *)(uintptr_t)0x29003U;
    init();
    uint32_t before = callbacks[2];
    assert(HAL_UART_Receive_DMA(&uart, rx, 0U) == HAL_ERROR);
    assert(HAL_UART_Transmit_IT(&uart, NULL, 1U) == HAL_ERROR);
    assert(HAL_UART_Receive_DMA(&uart, (void *)(uintptr_t)0x50000U, 1U) == HAL_ERROR);
    assert(uart.State == HAL_UART_STATE_READY && DMA->CHANNEL[1].EN == 0U);
    uart.hdmarx = &tx_dma;
    assert(HAL_UART_Receive_DMA(&uart, rx, 1U) == HAL_ERROR);
    uart.hdmarx = &rx_dma;
    rx_dma.Init.Request = DMA_REQUEST_SPI0;
    assert(HAL_UART_Receive_DMA(&uart, rx, 1U) == HAL_ERROR);
    rx_dma.Init.Request = tx_dma.Init.Request;
    assert(HAL_UART_Receive_DMA(&uart, rx, 0x2000000U) == HAL_ERROR);
    for (uint32_t mode = 0U; mode < 2U; ++mode)
    {
        init();
        assert((mode == 0U ? HAL_UART_Receive_IT(&uart, rx, 3U)
                           : HAL_UART_Receive_DMA(&uart, rx, 3U)) == HAL_OK);
        line_errors = HAL_UART_ERROR_PE;
        /* Polling TX must preserve clear-on-read LSR errors for active RX. */
        polling_clock = 1;
        HAL_UART_SetPollingTimeout(1U);
        assert(HAL_UART_Transmit(&uart, tx, 1U) == HAL_OK);
        polling_clock = 0;
        HAL_UART_Process(&uart);
        assert(uart.ErrorCode == HAL_UART_ERROR_PE && callbacks[2] == ++before);
    }
    init();
    tick = UINT32_MAX - 2U;
    assert(HAL_UART_SetAsyncTimeout(&uart, 5U) == HAL_OK);
    assert(HAL_UART_Receive_DMA(&uart, rx, 3U) == HAL_OK);
    assert(HAL_UART_Transmit_IT(&uart, tx, 1U) == HAL_OK);
    tick = 2U;
    HAL_UART_Process(&uart);
    assert(uart.State == HAL_UART_STATE_READY && uart.ErrorCode == HAL_UART_ERROR_TIMEOUT);
    assert(callbacks[2] == ++before && DMA->CHANNEL[1].EN == 0U);
    init();
    assert(HAL_UART_Receive_DMA(&uart, rx, 3U) == HAL_OK);
    assert(HAL_UART_Transmit_IT(&uart, tx, 1U) == HAL_OK);
    assert(HAL_UART_AbortReceive(&uart) == HAL_OK && uart.State == HAL_UART_STATE_BUSY_TX);
    assert(callbacks[2] == ++before && (uart.Instance->IER_DLH & IER_TX) != 0U);
    pump();
    init();
    assert(HAL_UART_Receive_DMA(&uart, rx, 3U) == HAL_OK);
    DMA->STATUS = 2U;
    assert(HAL_UART_AbortReceive(&uart) == HAL_TIMEOUT);
    assert(uart.State == HAL_UART_STATE_BUSY_RX && callbacks[2] == before);
    assert(HAL_UART_DeInit(&uart) == HAL_BUSY && HAL_UART_Receive_IT(&uart, rx, 1U) == HAL_BUSY);
    DMA->STATUS = 0U;
    HAL_UART_Process(&uart);
    assert(uart.State == HAL_UART_STATE_READY && callbacks[2] == ++before);
    assert((uart.ErrorCode & (HAL_UART_ERROR_ABORT | HAL_UART_ERROR_DMA)) ==
           (HAL_UART_ERROR_ABORT | HAL_UART_ERROR_DMA));
    init();
    assert(HAL_UART_Receive_DMA(&uart, rx, 3U) == HAL_OK);
    DMA->CHANNEL[1].LEFT = 2U;
    DMA->IRQ_PEND[0] |= 4U << 4;
    interrupts();
    HAL_UART_Process(&uart);
    assert(uart.ErrorCode == HAL_UART_ERROR_DMA && callbacks[2] == ++before);
    init();
    assert(HAL_UART_Receive_IT(&uart, rx, 1U) == HAL_OK);
    line_errors = HAL_UART_ERROR_ORE | HAL_UART_ERROR_FE | HAL_UART_ERROR_BI;
    interrupts();
    HAL_UART_Process(&uart);
    assert(uart.ErrorCode == (HAL_UART_ERROR_ORE | HAL_UART_ERROR_FE | HAL_UART_ERROR_BI));
    assert(callbacks[2] == ++before);
    init();
    assert(HAL_UART_SetAsyncTimeout(&uart, HAL_MAX_DELAY) == HAL_OK);
    assert(HAL_UART_Receive_IT(&uart, rx, 3U) == HAL_OK);
    tick += 1000000U;
    HAL_UART_Process(&uart);
    assert(uart.State == HAL_UART_STATE_BUSY_RX);
    assert(HAL_UART_Abort(&uart) == HAL_OK && callbacks[2] == ++before);
    UART_HandleTypeDef other = {
        .Instance = uart.Instance,
        .Init = {115200U, UART_WORDLENGTH_8B, UART_STOPBITS_1, UART_PARITY_NONE}};
    assert(HAL_UART_Init(&other) == HAL_BUSY);
    other.State = HAL_UART_STATE_READY;
    assert(HAL_UART_Transmit_IT(&other, tx, 1U) == HAL_BUSY);
    assert(HAL_UART_DeInit(&other) == HAL_BUSY);
}
static void polling_other_direction(void)
{
    uint8_t *tx = (void *)(uintptr_t)0x28001U, *rx = (void *)(uintptr_t)0x29003U;
    init();
    assert(HAL_UART_Receive_IT(&uart, rx, 1U) == HAL_OK);
    HAL_UART_SetPollingTimeout(1U);
    assert(HAL_UART_PrintString(&uart, "") == HAL_OK);
    assert(uart.State == HAL_UART_STATE_BUSY_RX);
    put_rx(0x37U);
    interrupts();
    HAL_UART_Process(&uart);
    init();
    assert(HAL_UART_Transmit_DMA(&uart, tx, 1U) == HAL_OK);
    put_rx(0x48U);
    HAL_UART_SetPollingTimeout(0U);
    assert(HAL_UART_Receive(&uart, rx, 1U) == HAL_OK && rx[0] == 0x48U);
    assert(uart.State == HAL_UART_STATE_BUSY_TX);
    pump();
}
static void map(uintptr_t address, size_t size)
{
    assert(mmap((void *)address, size, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == (void *)address);
}
int main(void)
{
    map(0x03000000U, 0x10000U);
    map(0x03021000U, 0x2000U);
    map(0x05000000U, 0x1000U);
    map(0x00020000U, 0x20000U);
    R328_REG32(R328_GICD_BASE + 4U) = 5U;
    assert(HAL_GIC_Init() == HAL_OK);
    uart.Instance = UART0;
    uart.Init = (UART_InitTypeDef){115200U, UART_WORDLENGTH_8B, UART_STOPBITS_1, UART_PARITY_NONE};
    assert(HAL_DMA_Init(&tx_dma) == HAL_OK && HAL_DMA_Init(&rx_dma) == HAL_OK);
    transfers();
    completion_order();
    guards_and_errors();
    polling_other_direction();
    assert(HAL_UART_DeInit(&uart) == HAL_OK);
    assert(HAL_DMA_DeInit(&rx_dma) == HAL_OK && HAL_DMA_DeInit(&tx_dma) == HAL_OK);
    uart.Instance = UART3;
    tx_dma.Init.Request = rx_dma.Init.Request = DMA_REQUEST_UART3;
    assert(HAL_DMA_Init(&tx_dma) == HAL_OK && HAL_DMA_Init(&rx_dma) == HAL_OK);
    transfers();
    assert(HAL_UART_DeInit(&uart) == HAL_OK);
    assert(HAL_DMA_DeInit(&rx_dma) == HAL_OK && HAL_DMA_DeInit(&tx_dma) == HAL_OK);
    puts("UART IT/DMA full duplex, FIFO tails, TEMT, IRQ order, timeout and abort tests passed");
    return 0;
}
