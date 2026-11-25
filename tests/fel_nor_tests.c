#include <assert.h>
#include <stdio.h>
#include <string.h>

#define R328_NOR_HOST_TEST 1
#include "../boot/nor/fel_nor.c"

volatile FelNorMailbox nor_test_mailbox;
static uint8_t flash[NOR_SIZE], buffer[FEL_NOR_BUFFER_SIZE];
static uint32_t registers[16], clock_register, bgr, gpio, pull, ticks;
static uint8_t fifo[64], frame[260], rx[64];
static uint32_t fifo_length, frame_length, rx_length, rx_offset, busy_cycles;
static uint32_t id, programs, erases, write_enables;
static uint8_t sr1, sr2;
static int selected, deny_wel, stuck_busy, fifo_fault, reset_fault;

uint8_t *nor_test_buffer(void)
{
    return buffer;
}
uint32_t nor_test_counter(void)
{
    ticks += 12000U;
    return ticks;
}

static uint32_t frame_address(void)
{
    assert(frame_length >= 4U);
    return ((uint32_t)frame[1] << 16) | ((uint32_t)frame[2] << 8) | frame[3];
}

static void commit(void)
{
    assert(frame_length != 0U);
    switch (frame[0])
    {
    case 0x06U:
        ++write_enables;
        if (!deny_wel)
        {
            sr1 |= 2U;
        }
        break;
    case 0xD8U:
    {
        assert((sr1 & 2U) != 0U && frame_length == 4U);
        uint32_t address = frame_address();
        assert((address & 0xFFFFU) == 0U && address <= NOR_SIZE - 0x10000U);
        memset(flash + address, 0xFF, 0x10000U);
        ++erases;
        sr1 &= ~2U;
        busy_cycles = 2U;
        break;
    }
    case 0x02U:
    {
        assert((sr1 & 2U) != 0U && frame_length > 4U && frame_length <= 260U);
        uint32_t address = frame_address(), count = frame_length - 4U;
        assert((address & 255U) + count <= 256U && address <= NOR_SIZE - count);
        for (uint32_t i = 0U; i < count; ++i)
        {
            flash[address + i] &= frame[4U + i];
        }
        ++programs;
        sr1 &= ~2U;
        busy_cycles = 2U;
        break;
    }
    default:
        break;
    }
    selected = 0;
    frame_length = 0U;
}

uint32_t nor_test_read32(uint32_t address)
{
    switch (address)
    {
    case CCU_CLK:
        return clock_register;
    case CCU_BGR:
        return bgr;
    case PC_CFG0:
        return gpio;
    case PC_PULL0:
        return pull;
    case SPI_BASE + 0x14U:
        return fifo_fault ? (1U << 8) : (1U << 12);
    case SPI_BASE + 0x1CU:
        return rx_length - rx_offset;
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
        clock_register = value;
        return;
    case CCU_BGR:
        bgr = value;
        if ((value & (1U << 16)) == 0U)
        {
            selected = 0;
            frame_length = 0U;
        }
        return;
    case PC_CFG0:
        gpio = value;
        return;
    case PC_PULL0:
        pull = value;
        return;
    case SPI_BASE + 0x14U:
        return;
    case SPI_BASE + 0x18U:
        fifo_length = rx_length = rx_offset = 0U;
        if (!reset_fault)
        {
            value &= ~FIFO_RESET;
        }
        break;
    case SPI_BASE + 0x08U:
        if ((value & XCH) != 0U)
        {
            assert(fifo_length == registers[0x34U / 4U] && fifo_length <= 64U);
            assert(registers[0x38U / 4U] == fifo_length);
            assert((value & (1U << 8)) != 0U);
            if (!selected)
            {
                selected = 1;
                frame_length = 0U;
            }
            assert(frame_length + fifo_length <= sizeof(frame));
            memcpy(frame + frame_length, fifo, fifo_length);
            frame_length += fifo_length;
            rx_length = registers[0x30U / 4U] - fifo_length;
            assert(rx_length <= sizeof(rx));
            for (uint32_t i = 0U; i < rx_length; ++i)
            {
                switch (frame[0])
                {
                case 0x9FU:
                    rx[i] = (uint8_t)(id >> (16U - i * 8U));
                    break;
                case 0x05U:
                    rx[i] = sr1 | (busy_cycles != 0U || stuck_busy ? 1U : 0U);
                    if (busy_cycles != 0U)
                    {
                        --busy_cycles;
                    }
                    break;
                case 0x35U:
                    rx[i] = sr2;
                    break;
                case 0x03U:
                    assert(frame_address() + i < NOR_SIZE);
                    rx[i] = flash[frame_address() + i];
                    break;
                default:
                    assert(0 && "unexpected read opcode");
                }
            }
            if ((value & (1U << 6)) == 0U)
            {
                commit(); /* automatic CS for the shared 03h reader */
            }
            value &= ~XCH;
        }
        else if (selected && (value & ((1U << 6) | (1U << 7))) == ((1U << 6) | (1U << 7)))
        {
            commit();
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
    assert(address == SPI_BASE + 0x300U && rx_offset < rx_length);
    return rx[rx_offset++];
}
void nor_test_write8(uint32_t address, uint8_t value)
{
    assert(address == SPI_BASE + 0x200U && fifo_length < sizeof(fifo));
    fifo[fifo_length++] = value;
}

static void request(uint32_t operation, uint32_t address, uint32_t length)
{
    nor_test_mailbox = (FelNorMailbox){.magic = FEL_NOR_MAGIC,
                                       .sequence = 17U,
                                       .operation = operation,
                                       .address = address,
                                       .length = length,
                                       .status = 0xFFFFFFFFU};
    uint32_t crc = 0xFFFFFFFFU;
    const volatile uint8_t *header = (const volatile uint8_t *)&nor_test_mailbox;
    for (uint32_t i = 0U; i < 20U; ++i)
    {
        crc = crc_update(crc, header[i]);
    }
    if (operation == FEL_NOR_PROGRAM && length <= sizeof(buffer))
    {
        for (uint32_t i = 0U; i < length; ++i)
        {
            crc = crc_update(crc, buffer[i]);
        }
    }
    nor_test_mailbox.crc = ~crc;
}

static void reset_model(void)
{
    memset(registers, 0, sizeof(registers));
    memset(flash, 0xA5, sizeof(flash));
    clock_register = gpio = pull = ticks = sr1 = sr2 = 0U;
    bgr = (1U << 17) | (1U << 1);
    fifo_length = frame_length = rx_length = rx_offset = busy_cycles = 0U;
    programs = erases = write_enables = 0U;
    selected = deny_wel = stuck_busy = fifo_fault = reset_fault = 0;
    id = 0xEF4017U;
}

static void dispatch_ok(void)
{
    fel_nor_dispatch();
    assert(nor_test_mailbox.status == FEL_NOR_OK);
    assert(nor_test_mailbox.completed == nor_test_mailbox.length);
    assert(clock_register == 0U && bgr == ((1U << 17) | (1U << 1)));
}

int main(void)
{
    reset_model();
    uint32_t crc = 0xFFFFFFFFU;
    for (const char *s = "123456789"; *s != '\0'; ++s)
    {
        crc = crc_update(crc, (uint8_t)*s);
    }
    assert(~crc == 0xCBF43926U);
    for (uint32_t variant = 0U; variant < 2U; ++variant)
    {
        id = variant ? 0xEF7017U : 0xEF4017U;
        request(FEL_NOR_PROBE, 0U, 0U);
        dispatch_ok();
        assert(nor_test_mailbox.detail == id);
    }
    request(FEL_NOR_ERASE, 0x10000U, 0x10000U);
    dispatch_ok();
    assert(flash[0xFFFF] == 0xA5 && flash[0x20000] == 0xA5 && erases == 1U);
    for (uint32_t i = 0x10000U; i < 0x20000U; ++i)
    {
        assert(flash[i] == 0xFFU);
    }
    for (uint32_t i = 0U; i < sizeof(buffer); ++i)
    {
        buffer[i] = (uint8_t)(i * 37U + (i >> 8));
    }
    uint8_t expected[sizeof(buffer)];
    memcpy(expected, buffer, sizeof(buffer));
    /* Unaligned start crosses 65 flash pages, each committed under one CS. */
    request(FEL_NOR_PROGRAM, 0x100FDU, sizeof(buffer));
    dispatch_ok();
    assert(programs == 65U && memcmp(flash + 0x100FDU, expected, sizeof(buffer)) == 0);
    memset(buffer, 0, sizeof(buffer));
    request(FEL_NOR_READ, 0x100FDU, sizeof(buffer));
    dispatch_ok();
    assert(memcmp(buffer, expected, sizeof(buffer)) == 0);

    const uint32_t invalid[][3] = {
        {FEL_NOR_ERASE, 1U, 0x10000U},        {FEL_NOR_ERASE, 0U, 0x20000U},
        {FEL_NOR_PROGRAM, NOR_SIZE - 1U, 2U}, {FEL_NOR_PROGRAM, 0U, FEL_NOR_BUFFER_SIZE + 1U},
        {FEL_NOR_READ, 0xFFFFFFF0U, 32U},     {FEL_NOR_READ, 0U, 0U}};
    for (uint32_t i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    {
        request(invalid[i][0], invalid[i][1], invalid[i][2]);
        fel_nor_dispatch();
        assert(nor_test_mailbox.status == FEL_NOR_RANGE);
    }
    request(99U, 0U, 0U);
    fel_nor_dispatch();
    assert(nor_test_mailbox.status == FEL_NOR_OPERATION);

    reset_model();
    request(FEL_NOR_PROGRAM, 0U, 256U);
    buffer[123] ^= 1U;
    fel_nor_dispatch();
    assert(nor_test_mailbox.status == FEL_NOR_CRC && write_enables == 0U);
    request(FEL_NOR_ERASE, 0U, 0x10000U);
    nor_test_mailbox.address ^= 0x10000U;
    fel_nor_dispatch();
    assert(nor_test_mailbox.status == FEL_NOR_CRC && erases == 0U);

    for (uint32_t fault = 0U; fault < 7U; ++fault)
    {
        reset_model();
        id = fault == 0U ? 0xEF4018U : 0xEF4017U;
        sr1 = fault == 1U ? 4U : 0U;
        sr2 = fault == 2U ? 0x40U : 0U;
        deny_wel = fault == 3U;
        stuck_busy = fault == 4U;
        fifo_fault = fault == 5U;
        reset_fault = fault == 6U;
        request(FEL_NOR_ERASE, 0U, 0x10000U);
        fel_nor_dispatch();
        const uint32_t statuses[] = {FEL_NOR_ID,        FEL_NOR_PROTECTED, FEL_NOR_PROTECTED,
                                     FEL_NOR_PROTECTED, FEL_NOR_BUSY,      FEL_NOR_SPI,
                                     FEL_NOR_SPI};
        assert(nor_test_mailbox.status == statuses[fault]);
        assert(erases == 0U && programs == 0U && clock_register == 0U);
    }
    puts(
        "FEL NOR helper: CRC, ID, protection, page/CS boundaries, erase/read/program, faults PASS");
    return 0;
}
