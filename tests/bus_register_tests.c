/* Deterministic MMIO model: validates command sequences, FIFO accesses, W1C,
 * timeouts and cleanup. It cannot validate pins, real clocks or audio timing. */
#include "bus_test_setup.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

static uint32_t tick;
static uint32_t audio_queued, audio_written, audio_read, audio_log[1024];
static uint32_t audio_tick_words;
uint32_t HAL_GetTick(void)
{
    /* Hardware keeps shifting while the CPU reads/divides the timebase.
     * Model this separately from MMIO polling to expose slow FIFO service. */
    if ((I2S0->CTL & 5U) == 5U && audio_tick_words != 0U)
    {
        if (audio_queued < audio_tick_words)
        {
            I2S0->ISTA |= HAL_I2S_ERROR_UDR;
            audio_queued = 0U;
        }
        else
        {
            audio_queued -= audio_tick_words;
        }
    }
    return tick++;
}
void Error_Handler(void)
{
    abort();
}
static int stuck_reset, stuck_pll, spi_stall, spi_error, twi_stall, twi_nack, twi_arlo, stop_stall,
    audio_stall;
static uint32_t audio_error;
static SPI_TypeDef *model_spi = SPI1;
static uint8_t spi_tx[64], spi_rx[64], spi_log[512];
static uint32_t spi_tx_count, spi_rx_count, spi_rx_pos, spi_log_count, spi_bursts, cs_asserts,
    cs_deasserts;
static uint32_t twi_active, twi_address, twi_read, twi_starts, twi_stops, twi_rx_count;
static uint8_t twi_log[64], twi_ack[64];
static uint32_t twi_log_count;
static int async_model;
static uint32_t completed, errors;
void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *h)
{
    assert(h->Lock == HAL_UNLOCKED);
    ++completed;
}
void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef *h)
{
    assert(h->Lock == HAL_UNLOCKED);
    ++completed;
}
void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *h)
{
    assert(h->Lock == HAL_UNLOCKED);
    ++completed;
}
void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *h)
{
    assert(h->Lock == HAL_UNLOCKED);
    ++completed;
}
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *h)
{
    assert(h->Lock == HAL_UNLOCKED);
    ++errors;
}

static void map_region(uintptr_t address, size_t size)
{
    assert(mmap((void *)address, size, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == (void *)address);
}
uint32_t R328_HAL_TestRead32(volatile uint32_t *reg)
{
    if (reg == &I2S0->FSTA)
    {
        if (!audio_stall && (I2S0->CTL & 5U) == 5U && audio_queued != 0U)
        {
            --audio_queued;
        }
        uint32_t free_space = audio_stall ? 0U : 128U - audio_queued;
        return (free_space << 16) | (free_space != 0U ? 1UL << 28 : 0U) |
               ((I2S0->CTL & 3U) == 3U ? (1U << 8) | 1U : 0U);
    }
    if (reg == &I2S0->ISTA && (I2S0->CTL & 1U) != 0U)
    {
        return *reg | audio_error;
    }
    if (reg == &I2S0->RXFIFO)
    {
        return 0xFFFF8000U + audio_read++;
    }
    return *reg;
}
void R328_HAL_TestWrite32(volatile uint32_t *reg, uint32_t value)
{
    if (reg == &DMA->IRQ_PEND[0] || reg == &DMA->IRQ_PEND[1])
    {
        *reg &= ~value;
        return;
    }
    if (reg == &I2C1->DRV_INT_CTRL)
    {
        *reg = (value & ~0xFU) | ((*reg & 0xFU) & ~(value & 0xFU));
        return;
    }
    if (reg == &I2C1->DRV_FIFO_CON)
    {
        *reg = stuck_reset ? value : 0U;
        return;
    }
    if (reg == &I2C1->DRV_SEND_FIFO)
    {
        assert(twi_log_count < sizeof(twi_log));
        twi_log[twi_log_count++] = (uint8_t)value;
    }
    if (reg == &RCC_PLL_AUDIO_CTRL || reg == &RCC_PLL_PERI0_CTRL)
    {
        *reg = value | (((value & (1UL << 31)) != 0U && !stuck_pll) ? 1UL << 28 : 0U);
        return;
    }
    if (reg == &model_spi->ISR || reg == &I2S0->ISTA)
    {
        *reg &= ~value;
        return;
    }
    if (reg == &model_spi->FCR)
    {
        if ((value & 0x80008000U) != 0U)
        {
            spi_tx_count = 0U;
            spi_rx_count = 0U;
            spi_rx_pos = 0U;
        }
        *reg = stuck_reset ? value : value & ~0x80008000U;
        model_spi->FSR = 0U;
        return;
    }
    if (reg == &model_spi->TCR)
    {
        uint32_t previous = *reg;
        if ((previous & 0x80U) != 0U && (value & 0x80U) == 0U)
        {
            ++cs_asserts;
        }
        if ((previous & 0x80U) == 0U && (value & 0x80U) != 0U)
        {
            ++cs_deasserts;
        }
        *reg = value;
        if ((value & (1UL << 31)) != 0U)
        {
            ++spi_bursts;
            assert(model_spi->MBC <= 64U);
            assert(spi_tx_count == model_spi->MTC);
            assert(model_spi->BCC == model_spi->MTC);
            if (!spi_stall)
            {
                spi_rx_count = (value & 0x100U) != 0U ? 0U : model_spi->MBC;
                for (uint32_t i = 0U; i < spi_rx_count; ++i)
                {
                    spi_rx[i] = spi_tx_count != 0U ? spi_tx[i] ^ 0xA5U : 0xFFU;
                }
                model_spi->FSR = spi_rx_count;
                model_spi->ISR |= spi_error ? 0x100U : 0x1000U;
                *reg &= ~(1UL << 31);
            }
        }
        return;
    }
    if (reg == &I2C1->SRST)
    {
        *reg = stuck_reset ? value : 0U;
        I2C1->STAT = 0xF8U;
        return;
    }
    if (reg == &I2C1->DATA)
    {
        assert(twi_log_count < sizeof(twi_log));
        twi_log[twi_log_count++] = (uint8_t)value;
    }
    if (reg == &I2C1->CNTR)
    {
        assert(async_model || (value & 0x80U) == 0U); /* Polling must not enable IRQ. */
        if ((value & 0x10U) != 0U)
        {
            ++twi_stops;
            *reg = stop_stall ? value & ~8U : 0x40U;
            if (!stop_stall)
            {
                I2C1->STAT = 0xF8U;
                twi_active = 0U;
            }
            return;
        }
        if (twi_stall)
        {
            *reg = value & ~8U;
            return;
        }
        if ((value & 0x20U) != 0U)
        {
            I2C1->STAT = twi_active ? 0x10U : 0x08U;
            twi_active = 1U;
            twi_address = 1U;
            ++twi_starts;
        }
        else if (twi_active && twi_address)
        {
            twi_address = 0U;
            twi_read = I2C1->DATA & 1U;
            I2C1->STAT = twi_arlo ? 0x38U : twi_nack ? 0x20U : twi_read ? 0x40U : 0x18U;
            if (twi_arlo)
            {
                twi_active = 0U;
            }
        }
        else if (twi_active && twi_read)
        {
            assert(twi_rx_count < sizeof(twi_ack));
            twi_ack[twi_rx_count] = (value & 4U) != 0U;
            I2C1->DATA = 0x60U + twi_rx_count++;
            I2C1->STAT = (value & 4U) != 0U ? 0x50U : 0x58U;
        }
        else if (twi_active)
        {
            I2C1->STAT = 0x28U;
        }
        *reg = value & ~0x28U;
        if (twi_active || twi_arlo)
        {
            *reg |= 8U;
        }
        return;
    }
    if (reg == &I2S0->FCTL)
    {
        if ((value & 0x3000000U) != 0U)
        {
            audio_queued = 0U;
        }
        *reg = stuck_reset ? value : value & ~0x3000000U;
        return;
    }
    if (reg == &I2S0->TXFIFO)
    {
        assert(audio_queued < 128U && audio_written < 1024U);
        audio_log[audio_written++] = value;
        ++audio_queued;
        ++I2S0->TXCNT; /* Counts writes, not samples sent on the wire. */
    }
    *reg = value;
}
uint8_t R328_HAL_TestRead8(volatile uint32_t *reg)
{
    assert(reg == &model_spi->RXD && spi_rx_pos < spi_rx_count);
    uint8_t result = spi_rx[spi_rx_pos++];
    model_spi->FSR = spi_rx_count - spi_rx_pos;
    return result;
}
void R328_HAL_TestWrite8(volatile uint32_t *reg, uint8_t value)
{
    if (reg == &I2C1->DRV_SEND_FIFO)
    {
        R328_HAL_TestWrite32(reg, value);
        return;
    }
    assert(reg == &model_spi->TXD && spi_tx_count < 64U && spi_log_count < sizeof(spi_log));
    spi_tx[spi_tx_count++] = value;
    spi_log[spi_log_count++] = value;
}
static void clear_twi_model(void)
{
    twi_active = 0U;
    twi_address = 0U;
    twi_read = 0U;
    twi_starts = 0U;
    twi_stops = 0U;
    twi_rx_count = 0U;
    twi_log_count = 0U;
    twi_nack = 0;
    twi_arlo = 0;
    twi_stall = 0;
    stop_stall = 0;
}
static void test_spi(void)
{
    assert(Test_SPI1_Init(1000000U) == HAL_OK);
    assert(hspi1.ActualBaudRateHz == 1000000U);
    assert((GPIOH->CFG[0] & 0xFFFF0000U) == 0x33310000U);
    assert((GPIOH->CFG[1] & 7U) == 1U);
    assert(Test_SPI1_Select(TEST_SPI_TOUCH) == HAL_OK);
    assert((GPIOH->DAT & 0x110U) == 0x10U);
    uint8_t tx[137], rx[137];
    for (uint32_t i = 0U; i < sizeof(tx); ++i)
    {
        tx[i] = (uint8_t)i;
    }
    HAL_SPI_SetPollingTimeout(100U);
    assert(HAL_SPI_TransmitReceive(&hspi1, tx, rx, sizeof(tx)) == HAL_OK);
    assert(spi_bursts == 3U && spi_log_count == sizeof(tx));
    assert(memcmp(tx, spi_log, sizeof(tx)) == 0);
    for (uint32_t i = 0U; i < sizeof(rx); ++i)
    {
        assert(rx[i] == (uint8_t)(tx[i] ^ 0xA5U));
    }
    assert((GPIOH->DAT & 0x110U) == 0x10U); /* Caller keeps GPIO CS through multiple calls. */
    assert(Test_SPI1_Select(TEST_SPI_NONE) == HAL_OK);
    HAL_SPI_SetPollingTimeout(100U);
    assert(HAL_SPI_Receive(&hspi1, rx, sizeof(rx)) == HAL_OK && rx[136] == 0xFFU);
    hspi1.Init.NSS = SPI_NSS_HARD_OUTPUT;
    assert(HAL_SPI_Init(&hspi1) == HAL_OK);
    cs_asserts = 0U;
    cs_deasserts = 0U;
    HAL_SPI_SetPollingTimeout(100U);
    assert(HAL_SPI_Transmit(&hspi1, tx, sizeof(tx)) == HAL_OK);
    assert(cs_asserts == 1U && cs_deasserts == 1U); /* No SS pulse at 64-byte boundaries. */
    hspi1.Lock = HAL_LOCKED;
    HAL_SPI_SetPollingTimeout(10U);
    assert(HAL_SPI_Transmit(&hspi1, tx, 1U) == HAL_BUSY);
    hspi1.Lock = HAL_UNLOCKED;
    HAL_SPI_SetPollingTimeout(10U);
    assert(HAL_SPI_TransmitReceive(&hspi1, tx, NULL, 1U) == HAL_ERROR);
    spi_stall = 1;
    tick = UINT32_MAX - 2U;
    HAL_SPI_SetPollingTimeout(5U);
    assert(HAL_SPI_Transmit(&hspi1, tx, 1U) == HAL_TIMEOUT);
    assert(hspi1.Lock == HAL_UNLOCKED && hspi1.State == HAL_SPI_STATE_RESET);
    spi_stall = 0;
    assert(HAL_SPI_Init(&hspi1) == HAL_OK);
    spi_error = 1;
    HAL_SPI_SetPollingTimeout(10U);
    assert(HAL_SPI_Transmit(&hspi1, tx, 1U) == HAL_ERROR);
    assert(hspi1.ErrorCode == HAL_SPI_ERROR_FIFO);
    spi_error = 0;
    assert(HAL_SPI_Init(&hspi1) == HAL_OK);
    stuck_reset = 1;
    HAL_SPI_SetPollingTimeout(10U);
    assert(HAL_SPI_Transmit(&hspi1, tx, 1U) == HAL_TIMEOUT);
    stuck_reset = 0;
    hspi1.Init.BaudRateHz = 999999U;
    assert(HAL_SPI_Init(&hspi1) == HAL_OK && hspi1.ActualBaudRateHz <= 999999U);
    hspi1.Init.BaudRateHz = 1U;
    assert(HAL_SPI_Init(&hspi1) == HAL_ERROR);
    assert(HAL_SPI_DeInit(&hspi1) == HAL_OK);
}
static void test_spi_clocks(void)
{
    /* The default LCD rate needs PLL_PERI0; the touch rate keeps OSC24M. */
    RCC_PLL_PERI0_CTRL = 0x08003100U;
    assert(Test_SPI1_Init(10000000U) == HAL_OK);
    assert(hspi1.ActualBaudRateHz == 10000000U);
    assert(RCC_SPI_CLK(1) == 0x81000002U && SPI1->CCR == 0x1009U);
    assert((RCC_PLL_PERI0_CTRL & 0xB800FFFFU) == 0xB8003100U);
    uint32_t pll = RCC_PLL_PERI0_CTRL;
    DMA_HandleTypeDef tx = {0}, rx = {0};
    hspi1.hdmatx = &tx;
    hspi1.hdmarx = &rx;
    assert(HAL_SPI_SetAsyncTimeout(&hspi1, 77U) == HAL_OK);
    const uint32_t rates[] = {25000000U,  40000000U, 50000000U, 60000000U,
                              100000000U, 999999U,   2000000U,  10000000U};
    for (uint32_t i = 0U; i < sizeof(rates) / sizeof(rates[0]); ++i)
    {
        uint32_t tcr = SPI1->TCR;
        assert(HAL_SPI_SetBaudRate(&hspi1, rates[i]) == HAL_OK);
        uint32_t module = (RCC_SPI_CLK(1) & (7U << 24)) == 0U ? R328_OSC24M_HZ : 200000000U;
        uint32_t divisor = (SPI1->CCR & (1U << 12)) != 0U ? 2U * ((SPI1->CCR & 255U) + 1U)
                                                          : 1U << ((SPI1->CCR >> 8) & 15U);
        assert((uint64_t)module <= (uint64_t)rates[i] * divisor);
        assert(hspi1.ActualBaudRateHz == module / divisor);
        assert((SPI1->TCR & ~0x2800U) == (tcr & ~0x2800U));
        uint32_t sample = hspi1.ActualBaudRateHz <= 24000000U   ? 0x2000U
                          : hspi1.ActualBaudRateHz >= 60000000U ? 0x0800U
                                                                : 0U;
        assert((SPI1->TCR & 0x2800U) == sample);
        assert(hspi1.hdmatx == &tx && hspi1.hdmarx == &rx && hspi1.XferTimeout == 77U);
        assert(RCC_PLL_PERI0_CTRL == pll); /* Reuse a running PLL without retuning. */
    }
    assert(HAL_SPI_SetBaudRate(&hspi1, 100000000U) == HAL_OK);
    assert(hspi1.ActualBaudRateHz == 100000000U);
    uint32_t clock = RCC_SPI_CLK(1), ccr = SPI1->CCR;
    assert(HAL_SPI_SetBaudRate(&hspi1, 100000001U) == HAL_ERROR);
    assert(HAL_SPI_SetBaudRate(&hspi1, 0U) == HAL_ERROR);
    assert(HAL_SPI_SetBaudRate(&hspi1, 1U) == HAL_ERROR);
    assert(RCC_SPI_CLK(1) == clock && SPI1->CCR == ccr);
    RCC_PLL_PERI0_CTRL = 0xB8003000U; /* Enabled at a different rate: preserve it. */
    assert(HAL_SPI_SetBaudRate(&hspi1, 10000000U) == HAL_ERROR);
    assert(RCC_PLL_PERI0_CTRL == 0xB8003000U && RCC_SPI_CLK(1) == clock && SPI1->CCR == ccr);
    assert(HAL_SPI_SetBaudRate(&hspi1, 2000000U) == HAL_OK);
    assert(hspi1.ActualBaudRateHz == 2000000U && RCC_SPI_CLK(1) == 0x80000000U);
    RCC_PLL_PERI0_CTRL = 0x08003100U;
    stuck_pll = 1;
    assert(HAL_SPI_SetBaudRate(&hspi1, 10000000U) == HAL_TIMEOUT);
    assert(RCC_PLL_PERI0_CTRL == 0x08003100U && RCC_SPI_CLK(1) == 0x80000000U);
    assert(hspi1.State == HAL_SPI_STATE_READY && hspi1.Lock == HAL_UNLOCKED);
    assert(hspi1.ActualBaudRateHz == 2000000U && hspi1.Init.BaudRateHz == 2000000U);
    stuck_pll = 0;
    assert(HAL_SPI_SetBaudRate(&hspi1, 10000000U) == HAL_OK);
    assert(HAL_SPI_DeInit(&hspi1) == HAL_OK);
    assert(HAL_SPI_SetBaudRate(&hspi1, 10000000U) == HAL_ERROR);
    hspi1.hdmatx = hspi1.hdmarx = NULL;
}
static void test_i2c(void)
{
    clear_twi_model();
    assert(Test_I2C1_Init(100000U) == HAL_OK);
    assert(hi2c1.ActualClockSpeed == 100000U);
    assert((GPIOB->CFG[1] & 0xFFU) == 0x44U);
    uint8_t data[3];
    HAL_I2C_SetPollingTimeout(100U);
    assert(HAL_I2C_Mem_Read(&hi2c1, 0x50U << 1, 0x1234U, 2U, data, 3U) == HAL_OK);
    const uint8_t expected[] = {0xA0U, 0x12U, 0x34U, 0xA1U};
    assert(twi_log_count == sizeof(expected) && memcmp(twi_log, expected, sizeof(expected)) == 0);
    assert(twi_starts == 2U && twi_stops == 1U); /* Repeated START, no intermediate STOP. */
    assert(twi_ack[0] == 1U && twi_ack[1] == 1U && twi_ack[2] == 0U);
    assert(data[0] == 0x60U && data[2] == 0x62U);
    clear_twi_model();
    HAL_I2C_SetPollingTimeout(100U);
    assert(HAL_I2C_Mem_Write(&hi2c1, 0xA0U, 0x12U, 1U, data, 3U) == HAL_OK);
    const uint8_t write_expected[] = {0xA0U, 0x12U, 0x60U, 0x61U, 0x62U};
    assert(twi_log_count == sizeof(write_expected) &&
           memcmp(twi_log, write_expected, sizeof(write_expected)) == 0);
    assert(twi_starts == 1U && twi_stops == 1U);
    clear_twi_model();
    HAL_I2C_SetPollingTimeout(100U);
    assert(HAL_I2C_Master_Receive(&hi2c1, 0xA0U, data, 1U) == HAL_OK);
    assert(twi_ack[0] == 0U && twi_log[0] == 0xA1U);
    clear_twi_model();
    twi_nack = 1;
    HAL_I2C_SetPollingTimeout(100U);
    assert(HAL_I2C_IsDeviceReady(&hi2c1, 0xA0U, 3U) == HAL_ERROR);
    assert(twi_starts == 3U && twi_stops == 3U && hi2c1.ErrorCode == HAL_I2C_ERROR_AF);
    clear_twi_model();
    twi_arlo = 1;
    HAL_I2C_SetPollingTimeout(100U);
    assert(HAL_I2C_Master_Transmit(&hi2c1, 0xA0U, data, 1U) == HAL_ERROR);
    assert(twi_stops == 0U && hi2c1.ErrorCode == HAL_I2C_ERROR_ARLO);
    clear_twi_model();
    twi_stall = 1;
    tick = UINT32_MAX - 1U;
    HAL_I2C_SetPollingTimeout(5U);
    assert(HAL_I2C_Master_Transmit(&hi2c1, 0xA0U, data, 1U) == HAL_TIMEOUT);
    assert(hi2c1.Lock == HAL_UNLOCKED && hi2c1.State == HAL_I2C_STATE_RESET);
    clear_twi_model();
    assert(Test_I2C1_Init(400000U) == HAL_OK && hi2c1.ActualClockSpeed == 400000U);
    stop_stall = 1;
    HAL_I2C_SetPollingTimeout(5U);
    assert(HAL_I2C_Master_Transmit(&hi2c1, 0xA0U, NULL, 0U) == HAL_TIMEOUT);
    assert(hi2c1.State == HAL_I2C_STATE_RESET);
    clear_twi_model();
    stuck_reset = 1;
    assert(Test_I2C1_Init(100000U) == HAL_TIMEOUT);
    stuck_reset = 0;
    assert(Test_I2C1_Init(100000U) == HAL_OK);
    HAL_I2C_SetPollingTimeout(10U);
    assert(HAL_I2C_Mem_Read(&hi2c1, 0xA1U, 0U, 1U, data, 1U) == HAL_ERROR);
    HAL_I2C_SetPollingTimeout(10U);
    assert(HAL_I2C_Mem_Read(&hi2c1, 0xA0U, 0x100U, 1U, data, 1U) == HAL_ERROR);
    HAL_I2C_SetPollingTimeout(10U);
    assert(HAL_I2C_Master_Receive(&hi2c1, 0xA0U, NULL, 0U) == HAL_ERROR);
    assert(HAL_I2C_DeInit(&hi2c1) == HAL_OK);
}
static void test_i2s(void)
{
    stuck_pll = 1;
    assert(HAL_RCC_AudioPLL_Config(RCC_AUDIO_FAMILY_48K) == HAL_TIMEOUT);
    stuck_pll = 0;
    RCC_I2S_CLK(1U) = 1UL << 31;
    assert(HAL_RCC_AudioPLL_Config(RCC_AUDIO_FAMILY_48K) == HAL_BUSY);
    RCC_I2S_CLK(1U) = 0U;
    assert(Test_I2S0_Init(48000U, 16U) == HAL_OK);
    assert(HAL_RCC_GetI2SClockFreq(I2S0) == 24576000U);
    assert((I2S0->CLKD & 0xF0U) == 0x50U); /* /8 -> BCLK 3.072MHz -> 48k stereo/32. */
    uint32_t pll = RCC_PLL_AUDIO_CTRL;
    RCC_PLL_AUDIO_CTRL ^= 1U << 16; /* A different post-divider is not the same recipe. */
    assert(HAL_RCC_GetAudioPLL4XFreq() == 0U);
    RCC_PLL_AUDIO_CTRL = pll;
    assert(I2S0->FMT0 == 0x1F37U && I2S0->RXCHMAP[3] == 0x100U);
    assert(I2S0->TXCHSEL[0] == 0x110003U && I2S0->TXCHMAP[0][1] == 0x10U);
    assert((GPIOB->CFG[0] & 0xFFFF00U) == 0x45500U); /* PB5 untouched. */
    assert(HAL_RCC_AudioPLL_Config(RCC_AUDIO_FAMILY_44K1) == HAL_BUSY);
    uint32_t tx[260], rx[260];
    for (uint32_t i = 0U; i < 260U; ++i)
    {
        tx[i] = i;
    }
    HAL_I2S_SetPollingTimeout(1000U);
    assert(HAL_I2S_Transmit(&hi2s0, tx, 260U) == HAL_OK);
    assert(audio_written == 260U && audio_queued == 0U && I2S0->TXCNT == 260U);
    assert(memcmp(tx, audio_log, sizeof(tx)) == 0);
    assert((I2S0->CTL & 7U) == 0U && hi2s0.Lock == HAL_UNLOCKED);
    /* Reproduce the original tone test's 960-sample blocks. Per-sample timebase
     * checks cannot keep up, while a FIFO-sized burst has sufficient slack. */
    uint32_t tone[960];
    for (uint32_t i = 0U; i < 960U; ++i)
    {
        tone[i] = i ^ 0x1234U;
    }
    audio_tick_words = 8U;
    for (uint32_t block = 0U; block < 20U; ++block)
    {
        audio_written = 0U;
        HAL_I2S_SetPollingTimeout(1000U);
        assert(HAL_I2S_Transmit(&hi2s0, tone, 960U) == HAL_OK);
        assert(audio_written == 960U && audio_queued == 0U);
        assert(memcmp(tone, audio_log, sizeof(tone)) == 0);
    }
    audio_tick_words = 0U;
    const uint32_t tx_errors[] = {HAL_I2S_ERROR_UDR, 1U << 5};
    for (uint32_t i = 0U; i < sizeof(tx_errors) / sizeof(tx_errors[0]); ++i)
    {
        audio_written = 0U;
        audio_error = tx_errors[i];
        HAL_I2S_SetPollingTimeout(1000U);
        assert(HAL_I2S_Transmit(&hi2s0, tx, 260U) == HAL_ERROR);
        assert(hi2s0.ErrorCode == tx_errors[i] && hi2s0.TxXferCount != 0U);
        assert(hi2s0.State == HAL_I2S_STATE_RESET && hi2s0.Lock == HAL_UNLOCKED);
        audio_error = 0U;
        assert(HAL_I2S_Init(&hi2s0) == HAL_OK);
        audio_written = 0U;
        HAL_I2S_SetPollingTimeout(1000U);
        assert(HAL_I2S_Transmit(&hi2s0, tx, 260U) == HAL_OK);
        assert(memcmp(tx, audio_log, sizeof(tx)) == 0);
    }
    HAL_I2S_SetPollingTimeout(100U);
    assert(HAL_I2S_Transmit(&hi2s0, tx, 1U) == HAL_ERROR);
    HAL_I2S_SetPollingTimeout(100U);
    assert(HAL_I2S_Receive(&hi2s0, rx, 2U) == HAL_ERROR);
    hi2s0.Init.Mode = I2S_MODE_MASTER_TX_RX;
    assert(HAL_I2S_Init(&hi2s0) == HAL_OK);
    audio_written = 0U;
    HAL_I2S_SetPollingTimeout(1000U);
    assert(HAL_I2S_TransmitReceive(&hi2s0, tx, rx, 260U) == HAL_OK);
    assert(audio_read == 260U && rx[0] == 0xFFFF8000U && rx[259] == 0xFFFF8103U);
    audio_written = 0U;
    audio_error = HAL_I2S_ERROR_OVR;
    HAL_I2S_SetPollingTimeout(20U);
    assert(HAL_I2S_Receive(&hi2s0, rx, 2U) == HAL_ERROR);
    assert(hi2s0.ErrorCode == HAL_I2S_ERROR_OVR && hi2s0.State == HAL_I2S_STATE_RESET);
    audio_error = 0U;
    assert(HAL_I2S_Init(&hi2s0) == HAL_OK);
    audio_stall = 1;
    tick = UINT32_MAX - 3U;
    HAL_I2S_SetPollingTimeout(10U);
    assert(HAL_I2S_Transmit(&hi2s0, tx, 2U) == HAL_TIMEOUT);
    assert(hi2s0.Lock == HAL_UNLOCKED && hi2s0.State == HAL_I2S_STATE_RESET);
    audio_stall = 0;
    assert(HAL_I2S_DeInit(&hi2s0) == HAL_OK);
    assert(Test_I2S0_Init(12345U, 16U) == HAL_ERROR);
    const uint32_t rates[] = {8000U,   12000U, 16000U, 24000U, 32000U, 48000U, 96000U,
                              192000U, 11025U, 22050U, 44100U, 88200U, 176400U};
    const uint32_t codes[] = {10U, 9U, 8U, 7U, 6U, 5U, 3U, 2U, 9U, 7U, 5U, 3U, 2U};
    for (uint32_t i = 0U; i < sizeof(rates) / sizeof(rates[0]); ++i)
    {
        assert(Test_I2S0_Init(rates[i], 16U) == HAL_OK);
        assert(((I2S0->CLKD >> 4) & 0xFU) == codes[i]);
        assert(HAL_I2S_DeInit(&hi2s0) == HAL_OK);
    }
    assert(Test_I2S0_Init(44100U, 24U) == HAL_OK);
    assert(HAL_RCC_GetI2SClockFreq(I2S0) == 22579200U);
    assert(I2S0->FMT0 == 0x1F57U && (I2S0->CLKD & 0xF0U) == 0x50U);
    hi2s0.Init.Standard = I2S_STANDARD_MSB;
    hi2s0.Init.MCLKOutput = I2S_MCLKOUTPUT_ENABLE;
    assert(HAL_I2S_Init(&hi2s0) == HAL_OK);
    assert(I2S0->TXCHSEL[0] == 0x10003U && I2S0->CLKD == 0x152U);
    assert(HAL_I2S_DeInit(&hi2s0) == HAL_OK);
}

static void pump_i2c(void)
{
    for (uint32_t i = 0U; hi2c1.State == HAL_I2C_STATE_BUSY && i < 100U; ++i)
    {
        HAL_I2C_IRQHandler(&hi2c1);
        HAL_I2C_Process(&hi2c1);
    }
    assert(hi2c1.State != HAL_I2C_STATE_BUSY);
}
static void model_dma_run(DMA_HandleTypeDef *h)
{
    uint32_t channel = h->Init.Channel;
    assert(DMA->CHANNEL[channel].EN == 1U);
    assert(DMA->CHANNEL[channel].MODE == 0xCU); /* DRQ handshake, including byte RX. */
    assert(DMA->CHANNEL[channel].DESC == (uint32_t)(uintptr_t)h->Descriptor);
    assert(h->Descriptor[5] == 0xFFFFF800U);
    if (h->Init.Direction == DMA_PERIPH_TO_MEMORY)
    {
        assert((h->Descriptor[0] & 0xFFFFU) == (46U | 1U << 8));
        assert(h->Descriptor[1] == (uint32_t)(uintptr_t)&I2C1->DRV_RECV_FIFO);
        uint8_t *out = (void *)(uintptr_t)h->Descriptor[2];
        for (uint32_t i = 0U; i < h->Descriptor[3]; ++i)
        {
            out[i] = (uint8_t)(0x60U + i);
        }
    }
    else
    {
        assert((h->Descriptor[0] >> 16) == (46U | 1U << 8));
        assert(h->Descriptor[2] == (uint32_t)(uintptr_t)&I2C1->DRV_SEND_FIFO);
        const uint8_t *in = (void *)(uintptr_t)h->Descriptor[1];
        for (uint32_t i = 0U; i < h->Descriptor[3]; ++i)
        {
            R328_HAL_TestWrite32(&I2C1->DRV_SEND_FIFO, in[i]);
        }
    }
    DMA->CHANNEL[channel].LEFT = 0U;
    DMA->CHANNEL[channel].EN = 0U;
    DMA->IRQ_PEND[channel / 8U] |= 4UL << ((channel % 8U) * 4U);
    HAL_DMA_IRQHandler(h);
}
static void packet_done(uint32_t error)
{
    I2C1->DRV_CTRL = error == 0U ? 0U : (1U << 24) | (error << 16);
    I2C1->DRV_INT_CTRL |= error == 0U ? 1U : 2U;
    HAL_I2C_IRQHandler(&hi2c1);
}
static void test_i2c_async(void)
{
    async_model = 1;
    clear_twi_model();
    assert(Test_I2C1_Init(100000U) == HAL_OK);
    assert(HAL_I2C_SetAsyncTimeout(&hi2c1, 1000U) == HAL_OK);
    uint8_t bytes[24];
    assert(HAL_I2C_Mem_Read_IT(&hi2c1, 0x76U << 1, 0x88U, 1U, bytes, sizeof(bytes)) == HAL_OK);
    assert(completed == 0U && hi2c1.State == HAL_I2C_STATE_BUSY && twi_log_count == 0U);
    assert(HAL_I2C_Master_Receive_IT(&hi2c1, 0xECU, bytes, 1U) == HAL_BUSY);
    assert(HAL_I2C_DeInit(&hi2c1) == HAL_BUSY && HAL_I2C_Init(&hi2c1) == HAL_BUSY);
    pump_i2c();
    assert(completed == 1U && errors == 0U && twi_starts == 2U && twi_stops == 1U);
    const uint8_t expected[] = {0xECU, 0x88U, 0xEDU};
    assert(twi_log_count == sizeof(expected) && memcmp(twi_log, expected, sizeof(expected)) == 0);
    for (uint32_t i = 0U; i < sizeof(bytes); ++i)
    {
        assert(bytes[i] == 0x60U + i);
        assert(twi_ack[i] == (i + 1U < sizeof(bytes)));
    }
    clear_twi_model();
    assert(HAL_I2C_Mem_Write_IT(&hi2c1, 0xECU, 0x1234U, 2U, bytes, 2U) == HAL_OK);
    pump_i2c();
    const uint8_t write[] = {0xECU, 0x12U, 0x34U, 0x60U, 0x61U};
    assert(twi_log_count == sizeof(write) && memcmp(twi_log, write, sizeof(write)) == 0);
    clear_twi_model();
    assert(HAL_I2C_Master_Receive_IT(&hi2c1, 0xECU, bytes, 1U) == HAL_OK);
    pump_i2c();
    assert(twi_ack[0] == 0U && twi_log[0] == 0xEDU);
    clear_twi_model();
    twi_nack = 1;
    assert(HAL_I2C_Master_Transmit_IT(&hi2c1, 0xECU, bytes, 1U) == HAL_OK);
    pump_i2c();
    assert(errors == 1U && hi2c1.ErrorCode == HAL_I2C_ERROR_AF && twi_stops == 1U);
    clear_twi_model();
    twi_arlo = 1;
    assert(HAL_I2C_Master_Transmit_IT(&hi2c1, 0xECU, bytes, 1U) == HAL_OK);
    pump_i2c();
    assert(errors == 2U && hi2c1.ErrorCode == HAL_I2C_ERROR_ARLO && twi_stops == 0U);
    clear_twi_model();
    twi_stall = 1;
    tick = UINT32_MAX - 3U;
    assert(HAL_I2C_SetAsyncTimeout(&hi2c1, 5U) == HAL_OK);
    assert(HAL_I2C_Master_Receive_IT(&hi2c1, 0xECU, bytes, 1U) == HAL_OK);
    pump_i2c();
    assert(errors == 3U && hi2c1.ErrorCode == HAL_I2C_ERROR_TIMEOUT);
    assert(hi2c1.State == HAL_I2C_STATE_RESET);
    clear_twi_model();
    assert(Test_I2C1_Init(100000U) == HAL_OK);
    assert(HAL_I2C_Master_Receive_IT(&hi2c1, 0xECU, bytes, 1U) == HAL_OK);
    assert(HAL_I2C_Abort(&hi2c1) == HAL_OK);
    assert(errors == 4U && hi2c1.State == HAL_I2C_STATE_RESET);
    clear_twi_model();
    assert(Test_I2C1_Init(100000U) == HAL_OK);
    assert(HAL_I2C_SetAsyncTimeout(&hi2c1, 1000U) == HAL_OK);
    static DMA_HandleTypeDef rx = {.Init = {8U, DMA_PERIPH_TO_MEMORY, DMA_REQUEST_TWI1}};
    static DMA_HandleTypeDef tx = {.Init = {1U, DMA_MEMORY_TO_PERIPH, DMA_REQUEST_TWI1}};
    assert(HAL_DMA_Init(&rx) == HAL_OK && HAL_DMA_Init(&tx) == HAL_OK);
    assert((RCC_DMA_BGR & 0x10001U) == 0x10001U && (RCC_MBUS_MAT_CLK_GATING & 1U) != 0U);
    DMA_HandleTypeDef other = {.Init = {8U, DMA_PERIPH_TO_MEMORY, DMA_REQUEST_TWI1}};
    assert(HAL_DMA_Init(&other) == HAL_BUSY); /* No channel stealing. */
    hi2c1.hdmarx = &rx;
    hi2c1.hdmatx = &tx;
    uint8_t *buffer = (void *)(uintptr_t)0x28000U;
    for (uint32_t order = 0U; order < 2U; ++order)
    {
        clear_twi_model();
        uint32_t before = completed;
        assert(HAL_I2C_Mem_Read_DMA(&hi2c1, 0xECU, 0xF7U, 1U, buffer, 6U) == HAL_OK);
        assert(I2C1->DRV_FMT == 0x10006U && I2C1->DRV_SLV == 0xED00U);
        assert((I2C1->DRV_CTRL & (1UL << 29)) == 0U); /* repeated START */
        assert(twi_log_count == 1U && twi_log[0] == 0xF7U);
        if (order == 0U)
        {
            model_dma_run(&rx);
        }
        else
        {
            packet_done(0U);
        }
        HAL_I2C_Process(&hi2c1);
        assert(completed == before && hi2c1.State == HAL_I2C_STATE_BUSY);
        if (order == 0U)
        {
            packet_done(0U);
        }
        else
        {
            model_dma_run(&rx);
        }
        HAL_I2C_Process(&hi2c1);
        assert(completed == before + 1U && hi2c1.State == HAL_I2C_STATE_READY);
        assert(buffer[0] == 0x60U && buffer[5] == 0x65U);
        HAL_I2C_Process(&hi2c1);
        assert(completed == before + 1U); /* exactly one callback */
    }
    clear_twi_model();
    assert(HAL_I2C_Mem_Write_DMA(&hi2c1, 0xECU, 0xF4U, 1U, buffer, 2U) == HAL_OK);
    model_dma_run(&tx);
    packet_done(0U);
    HAL_I2C_Process(&hi2c1);
    assert(twi_log_count == 3U && twi_log[0] == 0xF4U && twi_log[1] == 0x60U);
    assert(hi2c1.State == HAL_I2C_STATE_READY);
    clear_twi_model();
    assert(HAL_I2C_Mem_Read_DMA(&hi2c1, 0xECU, 0x1234U, 2U, buffer, 48U) == HAL_OK);
    assert(twi_log_count == 2U && twi_log[0] == 0x12U && twi_log[1] == 0x34U);
    model_dma_run(&rx);
    packet_done(0U);
    HAL_I2C_Process(&hi2c1);
    assert(buffer[47] == 0x8FU && hi2c1.State == HAL_I2C_STATE_READY);
    clear_twi_model();
    uint32_t residual_errors = errors, residual_completed = completed;
    assert(HAL_I2C_Mem_Read_DMA(&hi2c1, 0xECU, 0xF7U, 1U, buffer, 6U) == HAL_OK);
    model_dma_run(&rx);
    packet_done(0U);
    I2C1->DRV_FIFO_CON = 3U << 16; /* Queue ended but three sensor bytes remain. */
    HAL_I2C_Process(&hi2c1);
    assert(errors == residual_errors + 1U && completed == residual_completed);
    assert(hi2c1.ErrorCode == HAL_I2C_ERROR_DMA && hi2c1.State == HAL_I2C_STATE_RESET);
    I2C1->DRV_FIFO_CON = 0U;
    assert(Test_I2C1_Init(100000U) == HAL_OK);
    clear_twi_model();
    assert(HAL_I2C_Master_Transmit_DMA(&hi2c1, 0xECU, buffer, 48U) == HAL_OK);
    model_dma_run(&tx);
    packet_done(0U);
    HAL_I2C_Process(&hi2c1);
    assert(twi_log_count == 48U && twi_log[47] == 0x8FU);
    assert(HAL_I2C_Master_Receive_DMA(&hi2c1, 0xECU, buffer, 6U) == HAL_OK);
    assert((I2C1->DRV_CTRL & (1UL << 28)) != 0U && I2C1->DRV_FMT == 6U);
    packet_done(0x48U);
    HAL_I2C_Process(&hi2c1);
    assert(hi2c1.ErrorCode == HAL_I2C_ERROR_AF && rx.State == HAL_DMA_STATE_READY);
    assert(hi2c1.State == HAL_I2C_STATE_RESET);
    assert(Test_I2C1_Init(100000U) == HAL_OK);
    tick = UINT32_MAX - 3U;
    assert(HAL_I2C_SetAsyncTimeout(&hi2c1, 5U) == HAL_OK);
    assert(HAL_I2C_Mem_Read_DMA(&hi2c1, 0xECU, 0xF7U, 1U, buffer, 6U) == HAL_OK);
    pump_i2c(); /* Neither controller nor DMA completes. */
    assert(hi2c1.ErrorCode == HAL_I2C_ERROR_TIMEOUT && rx.State == HAL_DMA_STATE_READY);
    assert(DMA->CHANNEL[8].EN == 0U);
    assert(Test_I2C1_Init(100000U) == HAL_OK);
    assert(HAL_I2C_Mem_Read_DMA(&hi2c1, 0xECU, 0xF7U, 1U, buffer, 65536U) == HAL_ERROR);
    assert(HAL_I2C_Mem_Read_DMA(&hi2c1, 0xECU, 0xF7U, 1U, (void *)0xFFFFFFFEU, 6U) == HAL_ERROR);
    assert(hi2c1.Lock == HAL_UNLOCKED);
    assert(Test_I2C1_Init(100000U) == HAL_OK);
    assert(HAL_I2C_Mem_Read_DMA(&hi2c1, 0xECU, 0xF7U, 1U, buffer, 6U) == HAL_OK);
    uint32_t before_errors = errors;
    DMA->STATUS |= 1U << 8;
    assert(HAL_I2C_Abort(&hi2c1) == HAL_TIMEOUT);
    assert(hi2c1.State == HAL_I2C_STATE_BUSY && hi2c1.Lock == HAL_LOCKED);
    assert(errors == before_errors); /* No buffer release until DMA actually stops. */
    DMA->STATUS &= ~(1U << 8);
    HAL_I2C_Process(&hi2c1);
    assert(hi2c1.State == HAL_I2C_STATE_RESET && errors == before_errors + 1U);
    assert(Test_I2C1_Init(100000U) == HAL_OK);
    assert(HAL_I2C_Mem_Read_DMA(&hi2c1, 0xECU, 0xF7U, 1U, buffer, 6U) == HAL_OK);
    DMA->CHANNEL[8].LEFT = 3U; /* Queue completion with partial data is an error. */
    DMA->IRQ_PEND[1] |= 4U;
    HAL_DMA_IRQHandler(&rx);
    HAL_I2C_Process(&hi2c1);
    assert((hi2c1.ErrorCode & HAL_I2C_ERROR_DMA) != 0U && hi2c1.State == HAL_I2C_STATE_RESET);
    assert(HAL_DMA_DeInit(&rx) == HAL_OK && HAL_DMA_DeInit(&tx) == HAL_OK);
    hi2c1.hdmatx = NULL;
    hi2c1.hdmarx = NULL;
    assert(HAL_I2C_DeInit(&hi2c1) == HAL_OK);
    async_model = 0;
}
int main(void)
{
    map_region(0x03000000U, 0x10000U);
    map_region(0x03021000U, 0x2000U);
    map_region(0x00020000U, 0x20000U);
    R328_REG32(R328_GICD_BASE + 4U) = 5U;
    assert(HAL_GIC_Init() == HAL_OK);
    map_region(0x05002000U, 0x1000U);
    map_region(0x05010000U, 0x2000U);
    map_region(0x05090000U, 0x3000U);
    RCC_APB2_CFG = 0U;
    RCC_SPI_BGR = 0x10001U; /* Enabling SPI1 must preserve unrelated SPI0 bits. */
    GPIOH->CFG[0] = 0x33U;  /* Existing PH0/1 UART mux must survive. */
    test_spi();
    test_spi_clocks();
    test_i2c();
    test_i2c_async();
    test_i2s();
    assert((GPIOH->CFG[0] & 0xFFU) == 0x33U);
    assert((RCC_SPI_BGR & 0x10001U) == 0x10001U);
    puts("SPI / I2C / I2S sequences, FIFO, W1C, clock and failure-path tests passed (host model)");
    return 0;
}
