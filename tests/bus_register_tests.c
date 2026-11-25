/* Deterministic MMIO model: validates command sequences, FIFO accesses, W1C,
 * timeouts and cleanup. It cannot validate pins, real clocks or audio timing. */
#include "board.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

static uint32_t tick;
uint32_t HAL_GetTick(void)
{
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
static uint32_t audio_queued, audio_written, audio_read, audio_log[1024];

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
    if (reg == &RCC_PLL_AUDIO_CTRL)
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
        assert((value & 0x80U) == 0U); /* polling must not enable TWI IRQ */
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
    assert(Board_SPI1_Init(1000000U) == HAL_OK);
    assert(hspi1.ActualBaudRateHz == 1000000U);
    assert((GPIOH->CFG[0] & 0xFFFF0000U) == 0x33310000U);
    assert((GPIOH->CFG[1] & 7U) == 1U);
    assert(Board_SPI1_Select(BOARD_SPI_TOUCH) == HAL_OK);
    assert((GPIOH->DAT & 0x110U) == 0x10U);
    uint8_t tx[137], rx[137];
    for (uint32_t i = 0U; i < sizeof(tx); ++i)
    {
        tx[i] = (uint8_t)i;
    }
    assert(HAL_SPI_TransmitReceive(&hspi1, tx, rx, sizeof(tx), 100U) == HAL_OK);
    assert(spi_bursts == 3U && spi_log_count == sizeof(tx));
    assert(memcmp(tx, spi_log, sizeof(tx)) == 0);
    for (uint32_t i = 0U; i < sizeof(rx); ++i)
    {
        assert(rx[i] == (uint8_t)(tx[i] ^ 0xA5U));
    }
    assert((GPIOH->DAT & 0x110U) == 0x10U); /* Caller keeps GPIO CS through multiple calls. */
    assert(Board_SPI1_Select(BOARD_SPI_NONE) == HAL_OK);
    assert(HAL_SPI_Receive(&hspi1, rx, sizeof(rx), 100U) == HAL_OK && rx[136] == 0xFFU);
    hspi1.Init.NSS = SPI_NSS_HARD_OUTPUT;
    assert(HAL_SPI_Init(&hspi1) == HAL_OK);
    cs_asserts = 0U;
    cs_deasserts = 0U;
    assert(HAL_SPI_Transmit(&hspi1, tx, sizeof(tx), 100U) == HAL_OK);
    assert(cs_asserts == 1U && cs_deasserts == 1U); /* No SS pulse at 64-byte boundaries. */
    hspi1.Lock = HAL_LOCKED;
    assert(HAL_SPI_Transmit(&hspi1, tx, 1U, 10U) == HAL_BUSY);
    hspi1.Lock = HAL_UNLOCKED;
    assert(HAL_SPI_TransmitReceive(&hspi1, tx, NULL, 1U, 10U) == HAL_ERROR);
    spi_stall = 1;
    tick = UINT32_MAX - 2U;
    assert(HAL_SPI_Transmit(&hspi1, tx, 1U, 5U) == HAL_TIMEOUT);
    assert(hspi1.Lock == HAL_UNLOCKED && hspi1.State == HAL_SPI_STATE_RESET);
    spi_stall = 0;
    assert(HAL_SPI_Init(&hspi1) == HAL_OK);
    spi_error = 1;
    assert(HAL_SPI_Transmit(&hspi1, tx, 1U, 10U) == HAL_ERROR);
    assert(hspi1.ErrorCode == HAL_SPI_ERROR_FIFO);
    spi_error = 0;
    assert(HAL_SPI_Init(&hspi1) == HAL_OK);
    stuck_reset = 1;
    assert(HAL_SPI_Transmit(&hspi1, tx, 1U, 10U) == HAL_TIMEOUT);
    stuck_reset = 0;
    hspi1.Init.BaudRateHz = 999999U;
    assert(HAL_SPI_Init(&hspi1) == HAL_OK && hspi1.ActualBaudRateHz <= 999999U);
    hspi1.Init.BaudRateHz = 1U;
    assert(HAL_SPI_Init(&hspi1) == HAL_ERROR);
    assert(HAL_SPI_DeInit(&hspi1) == HAL_OK);
}
static void test_i2c(void)
{
    clear_twi_model();
    assert(Board_I2C1_Init(100000U) == HAL_OK);
    assert(hi2c1.ActualClockSpeed == 100000U);
    assert((GPIOB->CFG[1] & 0xFFU) == 0x44U);
    uint8_t data[3];
    assert(HAL_I2C_Mem_Read(&hi2c1, 0x50U << 1, 0x1234U, 2U, data, 3U, 100U) == HAL_OK);
    const uint8_t expected[] = {0xA0U, 0x12U, 0x34U, 0xA1U};
    assert(twi_log_count == sizeof(expected) && memcmp(twi_log, expected, sizeof(expected)) == 0);
    assert(twi_starts == 2U && twi_stops == 1U); /* Repeated START, no intermediate STOP. */
    assert(twi_ack[0] == 1U && twi_ack[1] == 1U && twi_ack[2] == 0U);
    assert(data[0] == 0x60U && data[2] == 0x62U);
    clear_twi_model();
    assert(HAL_I2C_Mem_Write(&hi2c1, 0xA0U, 0x12U, 1U, data, 3U, 100U) == HAL_OK);
    const uint8_t write_expected[] = {0xA0U, 0x12U, 0x60U, 0x61U, 0x62U};
    assert(twi_log_count == sizeof(write_expected) &&
           memcmp(twi_log, write_expected, sizeof(write_expected)) == 0);
    assert(twi_starts == 1U && twi_stops == 1U);
    clear_twi_model();
    assert(HAL_I2C_Master_Receive(&hi2c1, 0xA0U, data, 1U, 100U) == HAL_OK);
    assert(twi_ack[0] == 0U && twi_log[0] == 0xA1U);
    clear_twi_model();
    twi_nack = 1;
    assert(HAL_I2C_IsDeviceReady(&hi2c1, 0xA0U, 3U, 100U) == HAL_ERROR);
    assert(twi_starts == 3U && twi_stops == 3U && hi2c1.ErrorCode == HAL_I2C_ERROR_AF);
    clear_twi_model();
    twi_arlo = 1;
    assert(HAL_I2C_Master_Transmit(&hi2c1, 0xA0U, data, 1U, 100U) == HAL_ERROR);
    assert(twi_stops == 0U && hi2c1.ErrorCode == HAL_I2C_ERROR_ARLO);
    clear_twi_model();
    twi_stall = 1;
    tick = UINT32_MAX - 1U;
    assert(HAL_I2C_Master_Transmit(&hi2c1, 0xA0U, data, 1U, 5U) == HAL_TIMEOUT);
    assert(hi2c1.Lock == HAL_UNLOCKED && hi2c1.State == HAL_I2C_STATE_RESET);
    clear_twi_model();
    assert(Board_I2C1_Init(400000U) == HAL_OK && hi2c1.ActualClockSpeed == 400000U);
    stop_stall = 1;
    assert(HAL_I2C_Master_Transmit(&hi2c1, 0xA0U, NULL, 0U, 5U) == HAL_TIMEOUT);
    assert(hi2c1.State == HAL_I2C_STATE_RESET);
    clear_twi_model();
    stuck_reset = 1;
    assert(Board_I2C1_Init(100000U) == HAL_TIMEOUT);
    stuck_reset = 0;
    assert(Board_I2C1_Init(100000U) == HAL_OK);
    assert(HAL_I2C_Mem_Read(&hi2c1, 0xA1U, 0U, 1U, data, 1U, 10U) == HAL_ERROR);
    assert(HAL_I2C_Mem_Read(&hi2c1, 0xA0U, 0x100U, 1U, data, 1U, 10U) == HAL_ERROR);
    assert(HAL_I2C_Master_Receive(&hi2c1, 0xA0U, NULL, 0U, 10U) == HAL_ERROR);
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
    assert(Board_I2S0_Init(48000U, 16U) == HAL_OK);
    assert(HAL_RCC_GetI2SClockFreq(I2S0) == 294912000U);
    assert((I2S0->CLKD & 0xF0U) == 0xC0U); /* /96 -> BCLK 3.072MHz -> 48k stereo/32. */
    assert(I2S0->FMT0 == 0x1F37U && I2S0->RXCHMAP[3] == 0x100U);
    assert(I2S0->TXCHSEL[0] == 0x110003U && I2S0->TXCHMAP[0][1] == 0x10U);
    assert((GPIOB->CFG[0] & 0xFFFF00U) == 0x45500U); /* PB5 untouched. */
    assert(HAL_RCC_AudioPLL_Config(RCC_AUDIO_FAMILY_44K1) == HAL_BUSY);
    uint32_t tx[260], rx[260];
    for (uint32_t i = 0U; i < 260U; ++i)
    {
        tx[i] = i;
    }
    assert(HAL_I2S_Transmit(&hi2s0, tx, 260U, 1000U) == HAL_OK);
    assert(audio_written == 260U && audio_queued == 0U && I2S0->TXCNT == 260U);
    assert(memcmp(tx, audio_log, sizeof(tx)) == 0);
    assert((I2S0->CTL & 7U) == 0U && hi2s0.Lock == HAL_UNLOCKED);
    assert(HAL_I2S_Transmit(&hi2s0, tx, 1U, 100U) == HAL_ERROR);
    assert(HAL_I2S_Receive(&hi2s0, rx, 2U, 100U) == HAL_ERROR);
    hi2s0.Init.Mode = I2S_MODE_MASTER_TX_RX;
    assert(HAL_I2S_Init(&hi2s0) == HAL_OK);
    audio_written = 0U;
    assert(HAL_I2S_TransmitReceive(&hi2s0, tx, rx, 260U, 1000U) == HAL_OK);
    assert(audio_read == 260U && rx[0] == 0xFFFF8000U && rx[259] == 0xFFFF8103U);
    audio_written = 0U;
    audio_error = HAL_I2S_ERROR_OVR;
    assert(HAL_I2S_Receive(&hi2s0, rx, 2U, 20U) == HAL_ERROR);
    assert(hi2s0.ErrorCode == HAL_I2S_ERROR_OVR && hi2s0.State == HAL_I2S_STATE_RESET);
    audio_error = 0U;
    assert(HAL_I2S_Init(&hi2s0) == HAL_OK);
    audio_stall = 1;
    tick = UINT32_MAX - 3U;
    assert(HAL_I2S_Transmit(&hi2s0, tx, 2U, 10U) == HAL_TIMEOUT);
    assert(hi2s0.Lock == HAL_UNLOCKED && hi2s0.State == HAL_I2S_STATE_RESET);
    audio_stall = 0;
    assert(HAL_I2S_DeInit(&hi2s0) == HAL_OK);
    assert(Board_I2S0_Init(16000U, 16U) ==
           HAL_ERROR); /* No exact divider with this clock recipe. */
    assert(Board_I2S0_Init(44100U, 24U) == HAL_OK);
    assert(HAL_RCC_GetI2SClockFreq(I2S0) == 270950400U);
    assert(I2S0->FMT0 == 0x1F57U && (I2S0->CLKD & 0xF0U) == 0xC0U);
    hi2s0.Init.Standard = I2S_STANDARD_MSB;
    hi2s0.Init.MCLKOutput = I2S_MCLKOUTPUT_ENABLE;
    assert(HAL_I2S_Init(&hi2s0) == HAL_OK);
    assert(I2S0->TXCHSEL[0] == 0x10003U && I2S0->CLKD == 0x1C8U);
    assert(HAL_I2S_DeInit(&hi2s0) == HAL_OK);
}
int main(void)
{
    map_region(0x03000000U, 0x10000U);
    map_region(0x05002000U, 0x1000U);
    map_region(0x05010000U, 0x2000U);
    map_region(0x05090000U, 0x3000U);
    RCC_APB2_CFG = 0U;
    RCC_SPI_BGR = 0x10001U; /* Enabling SPI1 must preserve unrelated SPI0 bits. */
    GPIOH->CFG[0] = 0x33U;  /* Existing PH0/1 UART mux must survive. */
    test_spi();
    test_i2c();
    test_i2s();
    assert((GPIOH->CFG[0] & 0xFFU) == 0x33U);
    assert((RCC_SPI_BGR & 0x10001U) == 0x10001U);
    puts("SPI / I2C / I2S sequences, FIFO, W1C, clock and failure-path tests passed (host model)");
    return 0;
}
