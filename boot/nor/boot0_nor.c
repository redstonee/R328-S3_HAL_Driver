/* R328 boot0 NOR reader. No runtime, initialized data, heap or HAL dependency.
 * Linked into the unused NAND-reader region of the hash-locked prebuilt.
 * DDR training, TOC copying and direct application handoff stay in boot0.
 */
#include <stdint.h>

#define NOR_SIZE 0x00800000U
#define PACKAGE_OFFSET 0x00010000U
#define PACKAGE_BUFFER 0x42E00000U
#define SPI_BASE 0x05010000U
#define CCU_CLK 0x03001940U
#define CCU_BGR 0x0300196CU
#define PC_CFG0 0x0300B048U
#define PC_PULL0 0x0300B064U
#define XCH (1U << 31)
#define FIFO_RESET ((1U << 31) | (1U << 15))
#define POLL_LIMIT 1000000U
/* Mode 0, CS0 active low, controller-owned CS, discard command RX. */
#define TRANSFER_CONTROL ((1U << 13) | (1U << 9) | (1U << 8) | (1U << 2))

#ifdef R328_NOR_HOST_TEST
uint32_t nor_test_read32(uint32_t address);
void nor_test_write32(uint32_t address, uint32_t value);
uint8_t nor_test_read8(uint32_t address);
void nor_test_write8(uint32_t address, uint8_t value);
uint8_t *nor_test_buffer(void);
#define read32 nor_test_read32
#define write32 nor_test_write32
#define read8 nor_test_read8
#define write8 nor_test_write8
#define toc_buffer() nor_test_buffer()
#else
static uint32_t read32(uint32_t address)
{
    return *(volatile uint32_t *)(uintptr_t)address;
}
static void write32(uint32_t address, uint32_t value)
{
    *(volatile uint32_t *)(uintptr_t)address = value;
    __asm__ volatile("dsb sy" ::: "memory");
}
static uint8_t read8(uint32_t address)
{
    return *(volatile uint8_t *)(uintptr_t)address;
}
static void write8(uint32_t address, uint8_t value)
{
    *(volatile uint8_t *)(uintptr_t)address = value;
}
#define toc_buffer() ((uint8_t *)(uintptr_t)PACKAGE_BUFFER)
#endif

static int wait_clear(uint32_t address, uint32_t mask)
{
    for (uint32_t i = 0; i < POLL_LIMIT; ++i)
    {
        if ((read32(address) & mask) == 0U)
        {
            return 0;
        }
    }
    return -1;
}

static void spi_stop(void)
{
    /* Abort XCH even on timeout; keep SPI1 gate/reset untouched. */
    write32(CCU_BGR, read32(CCU_BGR) & ~((1U << 16) | 1U));
    write32(CCU_CLK, 0U);
}

static void spi_init(void)
{
    /* PC0 SCLK, PC2 MOSI, PC3 CS0, PC4 MISO, AF4. */
    write32(PC_CFG0, (read32(PC_CFG0) & ~0x000FFF0FU) | 0x00044404U);
    write32(PC_PULL0, (read32(PC_PULL0) & ~(3U << 6)) | (1U << 6));
    spi_stop();
    write32(CCU_CLK, 1U << 31); /* OSC24M / 1 */
    write32(CCU_BGR, read32(CCU_BGR) | 1U);
    write32(CCU_BGR, read32(CCU_BGR) | (1U << 16));
    write32(SPI_BASE + 0x04U, 0x83U);
    write32(SPI_BASE + 0x08U, TRANSFER_CONTROL);
    write32(SPI_BASE + 0x24U, 0x1000U); /* SCLK = OSC24M / 2 = 12 MHz */
    write32(SPI_BASE + 0x10U, 0U);      /* no IRQ / DMA */
    write32(SPI_BASE + 0x20U, 0U);
}

#ifndef R328_SPI_NO_NOR_READ
static int nor_read(uint32_t address, uint8_t *output, uint32_t length)
{
    if (address > NOR_SIZE || length > NOR_SIZE - address)
    {
        return -1;
    }
    while (length != 0U)
    {
        /* Whole command + receive fits the 64-byte FIFO. */
        uint32_t count = length > 60U ? 60U : length;
        write32(SPI_BASE + 0x18U, FIFO_RESET | (32U << 16) | 1U);
        if (wait_clear(SPI_BASE + 0x18U, FIFO_RESET) != 0)
        {
            return -1;
        }
        write32(SPI_BASE + 0x14U, 0x3F77U);
        write32(SPI_BASE + 0x30U, count + 4U);
        write32(SPI_BASE + 0x34U, 4U);
        write32(SPI_BASE + 0x38U, 4U);
        write8(SPI_BASE + 0x200U, 0x03U);
        write8(SPI_BASE + 0x200U, (uint8_t)(address >> 16));
        write8(SPI_BASE + 0x200U, (uint8_t)(address >> 8));
        write8(SPI_BASE + 0x200U, (uint8_t)address);
        write32(SPI_BASE + 0x08U, TRANSFER_CONTROL | XCH);
        uint32_t received = 0U, polls;
        for (polls = 0U; polls < POLL_LIMIT; ++polls)
        {
            uint32_t status = read32(SPI_BASE + 0x14U);
            if ((status & 0xF00U) != 0U)
            {
                return -1;
            }
            while (received < count && (read32(SPI_BASE + 0x1CU) & 0xFFU) != 0U)
            {
                output[received++] = read8(SPI_BASE + 0x300U);
            }
            if ((status & (1U << 12)) != 0U && (read32(SPI_BASE + 0x08U) & XCH) == 0U &&
                received == count)
            {
                break;
            }
        }
        if (polls == POLL_LIMIT)
        {
            return -1;
        }
        output += count;
        address += count;
        length -= count;
    }
    return 0;
}

#endif

#ifndef R328_NOR_SPI_ONLY
static int load_package(void)
{
    uint8_t *buffer = toc_buffer();
    uint32_t *words = (uint32_t *)buffer;
    if (nor_read(PACKAGE_OFFSET, buffer, 512U) != 0)
    {
        return -1;
    }
    uint32_t length = words[9];
    if (words[4] != 0x89119800U || words[8] != 1U || words[15] != 0x3B45494DU || length < 4096U ||
        (length & 4095U) != 0U || length > NOR_SIZE - PACKAGE_OFFSET || words[16] != 0x65657266U ||
        words[17] != 0x736F7472U || buffer[72] != 0U || words[32] != 512U || words[33] <= 512U ||
        words[33] > length - 512U || words[34] != 0U || words[35] != 3U ||
        words[36] != 0x40000000U || words[107] != 0x3B454949U)
    {
        return -1;
    }
    if (nor_read(PACKAGE_OFFSET + 512U, buffer + 512U, length - 512U) != 0 ||
        words[(512U + 0x2CU) / 4U] != 0x40000000U)
    {
        return -1;
    }
    /* Same TOC1 checksum as boot0; retain the stored checksum for its parser. */
    uint32_t sum = 0x5F0A6C39U - words[5];
    for (uint32_t i = 0; i < length / 4U; ++i)
    {
        sum += words[i];
    }
    return sum == words[5] ? 0 : -1;
}

__attribute__((section(".text.entry"))) int nor_boot_load(void)
{
    spi_init();
    int result = load_package();
    spi_stop();
    return result;
}
#endif
