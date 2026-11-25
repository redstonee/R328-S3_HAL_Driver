/* Shared SPI0 PIO transport for returning FEL flash helpers. */
#ifndef R328_FEL_SPI_H
#define R328_FEL_SPI_H
#define R328_NOR_SPI_ONLY 1
#include "../nor/boot0_nor.c"

enum
{
    FEL_SPI_OK = 0,
    FEL_SPI_ERROR = 4
};
#ifdef R328_NOR_HOST_TEST
uint32_t nor_test_counter(void);
#define counter_now() nor_test_counter()
#else
static uint32_t counter_now(void)
{
    uint32_t low, high;
    __asm__ volatile("mrrc p15, 0, %0, %1, c14" : "=r"(low), "=r"(high));
    return low;
}
#endif

static uint32_t crc_update(uint32_t crc, uint8_t byte)
{
    crc ^= byte;
    for (uint32_t i = 0U; i < 8U; ++i)
    {
        crc = (crc >> 1) ^ ((0U - (crc & 1U)) & 0xEDB88320U);
    }
    return crc;
}

/* TX may span FIFOs (page program); keep CS low across those bursts. */
static int command(const uint8_t *tx, uint32_t tx_length, uint8_t *rx, uint32_t rx_length)
{
    uint32_t control = TRANSFER_CONTROL | (1U << 6);
    uint32_t offset = 0U;
    do
    {
        uint32_t count = tx_length - offset;
        if (count > 64U)
        {
            count = 64U;
        }
        uint32_t receive = offset + count == tx_length ? rx_length : 0U;
        write32(SPI_BASE + 0x18U, FIFO_RESET | (32U << 16) | 1U);
        if (wait_clear(SPI_BASE + 0x18U, FIFO_RESET) != 0)
        {
            return FEL_SPI_ERROR;
        }
        write32(SPI_BASE + 0x14U, 0x3F77U);
        write32(SPI_BASE + 0x30U, count + receive);
        write32(SPI_BASE + 0x34U, count);
        write32(SPI_BASE + 0x38U, count);
        for (uint32_t i = 0U; i < count; ++i)
        {
            write8(SPI_BASE + 0x200U, tx[offset + i]);
        }
        write32(SPI_BASE + 0x08U, control | XCH);
        uint32_t received = 0U, polls;
        for (polls = 0U; polls < POLL_LIMIT; ++polls)
        {
            uint32_t status = read32(SPI_BASE + 0x14U);
            if ((status & 0xF00U) != 0U)
            {
                return FEL_SPI_ERROR;
            }
            while (received < receive && (read32(SPI_BASE + 0x1CU) & 0xFFU) != 0U)
            {
                rx[received++] = read8(SPI_BASE + 0x300U);
            }
            if ((status & (1U << 12)) != 0U && (read32(SPI_BASE + 0x08U) & XCH) == 0U &&
                received == receive)
            {
                break;
            }
        }
        if (polls == POLL_LIMIT)
        {
            return FEL_SPI_ERROR;
        }
        offset += count;
    } while (offset < tx_length);
    write32(SPI_BASE + 0x08U, control | (1U << 7)); /* command commits on CS rising */
    return FEL_SPI_OK;
}

#endif
