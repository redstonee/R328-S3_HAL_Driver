#include <assert.h>
#include <stdio.h>
#include <string.h>

#define R328_NOR_HOST_TEST 1
#include "../boot/nor/boot0_nor.c"

_Alignas(4) static uint8_t flash[NOR_SIZE];
static uint32_t staging[NOR_SIZE / 4U];
static uint32_t registers[0x40U / 4U];
static uint32_t ccu_clock, ccu_bgr, pc_config, pc_pull;
static uint8_t command[4];
static uint32_t command_count, rx_address, rx_remaining, transactions, status_reads;
static int stuck_reset, stuck_xch, no_tc, fifo_error, short_rx;

uint8_t *nor_test_buffer(void)
{
    return (uint8_t *)staging;
}

uint32_t nor_test_read32(uint32_t address)
{
    switch (address)
    {
    case CCU_CLK:
        return ccu_clock;
    case CCU_BGR:
        return ccu_bgr;
    case PC_CFG0:
        return pc_config;
    case PC_PULL0:
        return pc_pull;
    case SPI_BASE + 0x14U:
        ++status_reads;
        return fifo_error ? (1U << 8) : no_tc ? 0U : (1U << 12);
    case SPI_BASE + 0x1CU:
        /* Exercise receive polling and completion before all bytes arrive. */
        return short_rx || status_reads % 3U != 0U ? 0U : rx_remaining;
    default:
        assert(address >= SPI_BASE && address < SPI_BASE + sizeof(registers));
        return registers[(address - SPI_BASE) / 4U];
    }
}

void nor_test_write32(uint32_t address, uint32_t value)
{
    switch (address)
    {
    case CCU_CLK:
        ccu_clock = value;
        return;
    case CCU_BGR:
        ccu_bgr = value;
        if ((value & (1U << 16)) == 0U)
        {
            registers[0x08U / 4U] &= ~XCH;
        }
        return;
    case PC_CFG0:
        pc_config = value;
        return;
    case PC_PULL0:
        pc_pull = value;
        return;
    case SPI_BASE + 0x18U:
        command_count = 0U;
        rx_remaining = 0U;
        if (!stuck_reset)
        {
            value &= ~FIFO_RESET;
        }
        break;
    case SPI_BASE + 0x14U:
        return; /* W1C */
    case SPI_BASE + 0x08U:
        if ((value & XCH) != 0U)
        {
            assert(command_count == 4U && command[0] == 0x03U);
            assert((value & ~XCH) == TRANSFER_CONTROL);
            assert(registers[0x34U / 4U] == 4U && registers[0x38U / 4U] == 4U);
            assert(registers[0x30U / 4U] > 4U && registers[0x30U / 4U] <= 64U);
            rx_address = ((uint32_t)command[1] << 16) | ((uint32_t)command[2] << 8) | command[3];
            rx_remaining = registers[0x30U / 4U] - 4U;
            assert(rx_address <= NOR_SIZE - rx_remaining);
            ++transactions;
            if (!stuck_xch)
            {
                value &= ~XCH;
            }
        }
        break;
    default:
        break;
    }
    assert(address >= SPI_BASE && address < SPI_BASE + sizeof(registers));
    registers[(address - SPI_BASE) / 4U] = value;
}

uint8_t nor_test_read8(uint32_t address)
{
    assert(address == SPI_BASE + 0x300U && rx_remaining != 0U);
    --rx_remaining;
    return flash[rx_address++];
}

void nor_test_write8(uint32_t address, uint8_t value)
{
    assert(address == SPI_BASE + 0x200U && command_count < 4U);
    command[command_count++] = value;
}

static void reset_model(void)
{
    memset(registers, 0, sizeof(registers));
    memset(staging, 0xA5, sizeof(staging));
    ccu_clock = 0U;
    ccu_bgr = (1U << 17) | (1U << 1); /* SPI1 must survive boot0's SPI0 reset. */
    pc_config = 0x77777777U;
    pc_pull = 0U;
    command_count = rx_remaining = transactions = status_reads = 0U;
    stuck_reset = stuck_xch = no_tc = fifo_error = short_rx = 0;
}

static void make_package(void)
{
    uint8_t *buffer = flash + PACKAGE_OFFSET;
    for (uint32_t i = 0U; i < 8192U; ++i)
    {
        buffer[i] = (uint8_t)(i * 37U + (i >> 8));
    }
    uint32_t *words = (uint32_t *)buffer;
    words[4] = 0x89119800U;
    words[8] = 1U;
    words[9] = 8192U;
    words[15] = 0x3B45494DU;
    memcpy(buffer + 64U, "freertos", 9U);
    words[32] = 512U;
    words[33] = 7501U;
    words[34] = 0U;
    words[35] = 3U;
    words[36] = 0x40000000U;
    words[107] = 0x3B454949U;
    words[(512U + 0x2CU) / 4U] = 0x40000000U;
    words[5] = 0x5F0A6C39U;
    uint32_t sum = 0U;
    for (uint32_t i = 0U; i < 8192U / 4U; ++i)
    {
        sum += words[i];
    }
    words[5] = sum;
}

int main(void)
{
    reset_model();
    for (uint32_t i = 0U; i < NOR_SIZE; ++i)
    {
        flash[i] = (uint8_t)(i * 13U + (i >> 16));
    }
    uint8_t output[257];
    assert(nor_read(0x12FFFEU, output, sizeof(output)) == 0);
    assert(memcmp(output, flash + 0x12FFFEU, sizeof(output)) == 0);
    assert(transactions == 5U);
    assert(nor_read(NOR_SIZE - sizeof(output), output, sizeof(output)) == 0);
    assert(memcmp(output, flash + NOR_SIZE - sizeof(output), sizeof(output)) == 0);
    uint32_t before = transactions;
    assert(nor_read(NOR_SIZE, output, 0U) == 0);
    assert(nor_read(NOR_SIZE, output, 1U) == -1);
    assert(nor_read(NOR_SIZE + 1U, output, 0U) == -1);
    assert(nor_read(0xFFFFFFF0U, output, 32U) == -1);
    assert(transactions == before);

    reset_model();
    make_package();
    assert(nor_boot_load() == 0);
    assert(memcmp(staging, flash + PACKAGE_OFFSET, 8192U) == 0);
    assert(((uint8_t *)staging)[8192] == 0xA5U);
    assert(registers[0x24U / 4U] == 0x1000U);
    assert((pc_config & 0xFFF0FU) == 0x44404U);
    assert((pc_config & ~0xFFF0FU) == (0x77777777U & ~0xFFF0FU));
    assert(ccu_clock == 0U && ccu_bgr == ((1U << 17) | (1U << 1)));

    const uint32_t invalid_offsets[] = {16U,  32U,  36U,  60U,  64U,  72U,          128U,
                                        132U, 136U, 140U, 144U, 428U, 512U + 0x2CU, 7000U};
    for (uint32_t i = 0; i < sizeof(invalid_offsets) / sizeof(invalid_offsets[0]); ++i)
    {
        reset_model();
        make_package();
        flash[PACKAGE_OFFSET + invalid_offsets[i]] ^= 0x80U;
        assert(nor_boot_load() == -1);
        assert(ccu_clock == 0U);
    }
    for (uint32_t fault = 0U; fault < 5U; ++fault)
    {
        reset_model();
        make_package();
        stuck_reset = fault == 0U;
        stuck_xch = fault == 1U;
        no_tc = fault == 2U;
        fifo_error = fault == 3U;
        short_rx = fault == 4U;
        assert(nor_boot_load() == -1);
        assert(ccu_clock == 0U && (registers[0x08U / 4U] & XCH) == 0U);
    }
    puts("NOR boot reader: commands, FIFO, bounds, TOC, checksum and timeout PASS");
    return 0;
}
