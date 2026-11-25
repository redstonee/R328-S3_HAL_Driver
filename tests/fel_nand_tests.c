#include <assert.h>
#include <stdio.h>
#include <string.h>
#define R328_NOR_HOST_TEST 1
#include "../boot/nand/fel_nand.c"

volatile FelNandMailbox nand_test_mailbox;
static uint8_t flash[BOOT_AREA], buffer[FEL_NAND_BUFFER_SIZE], cache[2112], markers[32][2];
static uint32_t registers[16], clock_register, bgr, gpio, pull, ticks;
static uint8_t fifo[64], frame[2115], rx[64];
static uint32_t fifo_length, frame_length, rx_length, rx_offset, busy_cycles;
static uint32_t id, programs, erases, write_enables, page, ecc_status;
static uint8_t status, config, protection;
static int selected, deny_wel, stuck_busy, fifo_fault, reset_fault;
static int fail_program, fail_erase, deny_feature;

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
    assert(frame_length == 4U);
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
            status |= 2U;
        break;
    case 0x1FU:
        assert(frame_length == 3U);
        if (frame[1] == 0xA0U)
        {
            if (!deny_feature)
                protection = frame[2];
        }
        else
        {
            assert(frame[1] == 0xB0U && ((config ^ frame[2]) & 0xC0U) == 0U);
            if (!deny_feature)
                config = frame[2];
        }
        break;
    case 0x13U:
        page = frame_address();
        assert(page < BOOT_AREA / NAND_PAGE);
        memcpy(cache, flash + page * NAND_PAGE, NAND_PAGE);
        memset(cache + NAND_PAGE, 0xFF, 64U);
        if (page % 64U < 2U)
            cache[NAND_PAGE] = markers[page / 64U][page % 64U];
        status = (uint8_t)((status & 2U) | ecc_status);
        busy_cycles = 2U;
        break;
    case 0x02U:
        assert((status & 2U) != 0U && frame_length == NAND_PAGE + 3U);
        assert(frame[1] == 0U && frame[2] == 0U);
        memset(cache, 0xFF, sizeof(cache));
        memcpy(cache, frame + 3U, NAND_PAGE);
        break;
    case 0x10U:
        page = frame_address();
        assert((status & 2U) != 0U && (config & 0x50U) == 0x10U && protection == 0U);
        assert(page < BOOT_AREA / NAND_PAGE);
        for (uint32_t i = NAND_PAGE; i < sizeof(cache); ++i)
            assert(cache[i] == 0xFFU);
        if (!fail_program)
            for (uint32_t i = 0U; i < NAND_PAGE; ++i)
                flash[page * NAND_PAGE + i] &= cache[i];
        ++programs;
        status = fail_program ? 8U : 0U;
        busy_cycles = 2U;
        break;
    case 0xD8U:
        page = frame_address();
        assert((status & 2U) != 0U && protection == 0U && page % 64U == 0U);
        assert(page < BOOT_AREA / NAND_PAGE);
        if (!fail_erase)
            memset(flash + page * NAND_PAGE, 0xFF, NAND_BLOCK);
        ++erases;
        status = fail_erase ? 4U : 0U;
        busy_cycles = 2U;
        break;
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
                    assert(frame_length == 2U && frame[1] == 0U && rx_length == 2U);
                    rx[i] = (uint8_t)(id >> (8U - i * 8U));
                    break;
                case 0x0FU:
                    assert(frame_length == 2U && rx_length == 1U);
                    if (frame[1] == 0xA0U)
                        rx[i] = protection;
                    else if (frame[1] == 0xB0U)
                        rx[i] = config;
                    else
                    {
                        assert(frame[1] == 0xC0U);
                        rx[i] = status | (busy_cycles != 0U || stuck_busy ? 1U : 0U);
                        if (busy_cycles != 0U)
                            --busy_cycles;
                    }
                    break;
                case 0x03U:
                {
                    assert(frame_length == 4U && frame[3] == 0U);
                    uint32_t column = ((uint32_t)frame[1] << 8) | frame[2];
                    assert(column + i < sizeof(cache));
                    rx[i] = cache[column + i];
                    break;
                }
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
    nand_test_mailbox = (FelNandMailbox){.magic = FEL_NAND_MAGIC,
                                         .sequence = 17U,
                                         .operation = operation,
                                         .address = address,
                                         .length = length,
                                         .status = 0xFFFFFFFFU};
    uint32_t crc = 0xFFFFFFFFU;
    const volatile uint8_t *header = (const volatile uint8_t *)&nand_test_mailbox;
    for (uint32_t i = 0U; i < 20U; ++i)
    {
        crc = crc_update(crc, header[i]);
    }
    if (operation == FEL_NAND_PROGRAM && length <= sizeof(buffer))
    {
        for (uint32_t i = 0U; i < length; ++i)
        {
            crc = crc_update(crc, buffer[i]);
        }
    }
    nand_test_mailbox.crc = ~crc;
}

static void reset_model(void)
{
    memset(registers, 0, sizeof(registers));
    memset(flash, 0xA5, sizeof(flash));
    memset(markers, 0xFF, sizeof(markers));
    clock_register = gpio = pull = ticks = status = 0U;
    config = 0U;
    protection = 0x38U; /* ECC initially disabled, power-on BP lock. */
    bgr = (1U << 17) | (1U << 1);
    fifo_length = frame_length = rx_length = rx_offset = busy_cycles = 0U;
    programs = erases = write_enables = ecc_status = 0U;
    selected = deny_wel = stuck_busy = fifo_fault = reset_fault = 0;
    fail_program = fail_erase = deny_feature = 0;
    id = 0xC212U;
}
static void dispatch_ok(void)
{
    fel_nand_dispatch();
    assert(nand_test_mailbox.status == FEL_NAND_OK);
    assert(nand_test_mailbox.completed == nand_test_mailbox.length);
    assert(clock_register == 0U && bgr == ((1U << 17) | (1U << 1)));
    assert(protection == 0x38U);
}
int main(void)
{
    reset_model();
    request(FEL_NAND_PROBE, 0U, 0U);
    dispatch_ok();
    assert(nand_test_mailbox.detail == 0xC212U && config == 0U && erases == 0U);
    markers[1][1] = 0U;
    request(FEL_NAND_BAD_BLOCK, NAND_BLOCK, 0U);
    dispatch_ok();
    assert(nand_test_mailbox.detail == 0xFFU && config == 0x10U);
    request(FEL_NAND_ERASE, NAND_BLOCK, NAND_BLOCK);
    fel_nand_dispatch();
    assert(nand_test_mailbox.status == FEL_NAND_BAD && erases == 0U);
    markers[1][1] = 0xFFU;
    request(FEL_NAND_ERASE, NAND_BLOCK, NAND_BLOCK);
    dispatch_ok();
    assert(flash[NAND_BLOCK - 1U] == 0xA5U && flash[NAND_BLOCK * 2U] == 0xA5U);
    for (uint32_t i = NAND_BLOCK; i < NAND_BLOCK * 2U; ++i)
        assert(flash[i] == 0xFFU);
    uint8_t expected[sizeof(buffer)];
    for (uint32_t i = 0U; i < sizeof(buffer); ++i)
        expected[i] = buffer[i] = (uint8_t)(i * 37U + (i >> 8));
    request(FEL_NAND_PROGRAM, NAND_BLOCK + NAND_PAGE, sizeof(buffer));
    dispatch_ok();
    assert(programs == 8U && memcmp(flash + NAND_BLOCK + NAND_PAGE, expected, sizeof(buffer)) == 0);
    memset(buffer, 0, sizeof(buffer));
    ecc_status = 0x10U; /* Corrected pages remain readable. */
    request(FEL_NAND_READ, NAND_BLOCK + NAND_PAGE, sizeof(buffer));
    dispatch_ok();
    assert(memcmp(buffer, expected, sizeof(buffer)) == 0);
    request(FEL_NAND_READ, NAND_BLOCK + NAND_PAGE + 33U, 2051U);
    dispatch_ok();
    assert(memcmp(buffer, expected + 33U, 2051U) == 0);
    for (uint32_t value = 0x20U; value <= 0x30U; value += 0x10U)
    {
        ecc_status = value;
        request(FEL_NAND_READ, NAND_BLOCK, NAND_PAGE);
        fel_nand_dispatch();
        assert(nand_test_mailbox.status == FEL_NAND_ECC && nand_test_mailbox.completed == 0U);
        request(FEL_NAND_BAD_BLOCK, NAND_BLOCK, 0U);
        dispatch_ok(); /* M2 marker outside ECC. */
    }
    const uint32_t invalid[][3] = {{FEL_NAND_ERASE, 1U, NAND_BLOCK},
                                   {FEL_NAND_ERASE, 0U, 0x10000U},
                                   {FEL_NAND_ERASE, BOOT_AREA, NAND_BLOCK},
                                   {FEL_NAND_PROGRAM, BOOT_AREA, NAND_PAGE},
                                   {FEL_NAND_PROGRAM, 1U, NAND_PAGE},
                                   {FEL_NAND_PROGRAM, 0U, NAND_PAGE - 1U},
                                   {FEL_NAND_PROGRAM, NAND_BLOCK - NAND_PAGE, 2U * NAND_PAGE},
                                   {FEL_NAND_READ, NAND_SIZE - 1U, 2U},
                                   {FEL_NAND_READ, 0U, FEL_NAND_BUFFER_SIZE + 1U},
                                   {FEL_NAND_BAD_BLOCK, NAND_SIZE, 0U},
                                   {FEL_NAND_BAD_BLOCK, 1U, 0U},
                                   {FEL_NAND_BAD_BLOCK, 0U, 1U},
                                   {FEL_NAND_READ, 0U, 0U}};
    for (uint32_t i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    {
        request(invalid[i][0], invalid[i][1], invalid[i][2]);
        fel_nand_dispatch();
        assert(nand_test_mailbox.status == FEL_NAND_RANGE);
    }
    reset_model();
    request(FEL_NAND_PROGRAM, 0U, NAND_PAGE);
    buffer[123] ^= 1U;
    fel_nand_dispatch();
    assert(nand_test_mailbox.status == FEL_NAND_CRC && write_enables == 0U);
    request(FEL_NAND_ERASE, 0U, NAND_BLOCK);
    nand_test_mailbox.address ^= NAND_BLOCK;
    fel_nand_dispatch();
    assert(nand_test_mailbox.status == FEL_NAND_CRC && erases == 0U);
    for (uint32_t fault = 0U; fault < 11U; ++fault)
    {
        reset_model();
        if (fault == 0U)
            id = 0xC214U;
        if (fault == 1U)
            protection = 0x39U;
        if (fault == 2U)
            protection = 0xB8U;
        deny_wel = fault == 3U;
        stuck_busy = fault == 4U;
        fifo_fault = fault == 5U;
        reset_fault = fault == 6U;
        if (fault == 7U)
            config = 0x50U;
        deny_feature = fault == 8U;
        fail_erase = fault == 9U;
        fail_program = fault == 10U;
        request(fault == 10U ? FEL_NAND_PROGRAM : FEL_NAND_ERASE, 0U,
                fault == 10U ? NAND_PAGE : NAND_BLOCK);
        fel_nand_dispatch();
        const uint32_t statuses[] = {FEL_NAND_ID,         FEL_NAND_PROTECTED,   FEL_NAND_PROTECTED,
                                     FEL_NAND_PROTECTED,  FEL_NAND_BUSY,        FEL_NAND_SPI,
                                     FEL_NAND_SPI,        FEL_NAND_FEATURE,     FEL_NAND_FEATURE,
                                     FEL_NAND_ERASE_FAIL, FEL_NAND_PROGRAM_FAIL};
        assert(nand_test_mailbox.status == statuses[fault]);
        if (fault >= 9U)
            assert(protection == 0x38U);
        else
            assert(erases == 0U && programs == 0U);
        assert(clock_register == 0U);
    }
    puts("FEL NAND: ID/dummy, marker pages, ECC, BP restore, CS/page/block boundaries, "
         "CRC/failures PASS");
    return 0;
}
