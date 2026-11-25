/* MX35LF1GE4AB main-area programmer; each command returns to BROM FEL. */
#include "fel_nand.h"
#define R328_SPI_NO_NOR_READ 1
#include "../common/fel_spi.h"

#define NAND_PAGE 2048U
#define NAND_BLOCK 0x20000U
#define NAND_SIZE 0x8000000U
#define BOOT_AREA 0x400000U

#ifdef R328_NOR_HOST_TEST
extern volatile FelNandMailbox nand_test_mailbox;
#define mailbox() (&nand_test_mailbox)
#define transfer_buffer() nor_test_buffer()
#else
#define mailbox() ((volatile FelNandMailbox *)(uintptr_t)FEL_NAND_MAILBOX)
#define transfer_buffer() ((uint8_t *)(uintptr_t)FEL_NAND_BUFFER)
#endif

static int get_feature(uint8_t address, uint8_t *value)
{
    uint8_t tx[2] = {0x0FU, address};
    return command(tx, sizeof(tx), value, 1U);
}

static int set_feature(uint8_t address, uint8_t value)
{
    uint8_t tx[3] = {0x1FU, address, value}, actual;
    int result = command(tx, sizeof(tx), 0, 0U);
    if (result == FEL_NAND_OK)
    {
        result = get_feature(address, &actual);
        if (result == FEL_NAND_OK && actual != value)
        {
            result = FEL_NAND_FEATURE;
        }
    }
    return result;
}

static int wait_ready(uint8_t *status)
{
    uint32_t start = counter_now();
    for (uint32_t polls = 0U; polls < POLL_LIMIT; ++polls)
    {
        int result = get_feature(0xC0U, status);
        if (result != FEL_NAND_OK || (*status & 1U) == 0U)
        {
            return result;
        }
        /* 1 s also bounds erase; unsigned subtraction tolerates counter wrap. */
        if (counter_now() - start >= 24000000U)
        {
            break;
        }
    }
    return FEL_NAND_BUSY;
}

static int row_command(uint8_t opcode, uint32_t row, uint8_t *status)
{
    uint8_t tx[4] = {opcode, (uint8_t)(row >> 16), (uint8_t)(row >> 8), (uint8_t)row};
    int result = command(tx, sizeof(tx), 0, 0U);
    return result == FEL_NAND_OK ? wait_ready(status) : result;
}

static int read_cache(uint32_t column, uint8_t *data, uint32_t length)
{
    /* Separate cache commands keep each receive burst within the 64-byte FIFO. */
    while (length != 0U)
    {
        uint32_t count = length > 60U ? 60U : length;
        uint8_t tx[4] = {0x03U, (uint8_t)(column >> 8), (uint8_t)column, 0U};
        int result = command(tx, sizeof(tx), data, count);
        if (result != FEL_NAND_OK)
        {
            return result;
        }
        column += count;
        data += count;
        length -= count;
    }
    return FEL_NAND_OK;
}

static int bad_block(uint32_t address, uint32_t *markers)
{
    uint8_t status, bytes[2];
    for (uint32_t i = 0U; i < 2U; ++i)
    {
        int result = row_command(0x13U, address / NAND_PAGE + i, &status);
        if (result == FEL_NAND_OK)
        {
            /* Spare byte 0 is outside ECC coverage (M2), even on a bad page.
             * Ignore page ECC status only for these factory marker reads. */
            result = read_cache(NAND_PAGE, &bytes[i], 1U);
        }
        if (result != FEL_NAND_OK)
        {
            return result;
        }
    }
    *markers = bytes[0] | ((uint32_t)bytes[1] << 8);
    return FEL_NAND_OK;
}

static int write_enable(void)
{
    uint8_t opcode = 0x06U, status;
    int result = command(&opcode, 1U, 0, 0U);
    if (result == FEL_NAND_OK)
    {
        result = get_feature(0xC0U, &status);
        if (result == FEL_NAND_OK && (status & 2U) == 0U)
        {
            result = FEL_NAND_PROTECTED;
        }
    }
    return result;
}

static int transfer(const FelNandMailbox *request, uint32_t *detail, uint32_t *completed)
{
    uint32_t address = request->address, length = request->length;
    uint8_t status;
    if (request->operation == FEL_NAND_ERASE)
    {
        int result = write_enable();
        if (result == FEL_NAND_OK)
        {
            result = row_command(0xD8U, address / NAND_PAGE, &status);
            if (result == FEL_NAND_OK && (status & 4U) != 0U)
            {
                result = FEL_NAND_ERASE_FAIL;
            }
        }
        if (result == FEL_NAND_OK)
        {
            *completed = length;
        }
        return result;
    }
    uint8_t tx[NAND_PAGE + 3U];
    while (*completed < length)
    {
        uint32_t column = address & (NAND_PAGE - 1U);
        uint32_t count = NAND_PAGE - column;
        if (count > length - *completed)
        {
            count = length - *completed;
        }
        int result;
        if (request->operation == FEL_NAND_READ)
        {
            result = row_command(0x13U, address / NAND_PAGE, &status);
            if (result == FEL_NAND_OK && (status & 0x30U) >= 0x20U)
            {
                *detail = address / NAND_PAGE;
                return FEL_NAND_ECC;
            }
            if (result == FEL_NAND_OK)
            {
                result = read_cache(column, transfer_buffer() + *completed, count);
            }
        }
        else
        {
            tx[0] = 0x02U; /* PROGRAM LOAD resets the entire cache to FF first. */
            tx[1] = tx[2] = 0U;
            for (uint32_t i = 0U; i < NAND_PAGE; ++i)
            {
                tx[i + 3U] = transfer_buffer()[*completed + i];
            }
            /* Whole main page, one execute; unused metadata stays FF in cache.
             * ECC parity goes to the chip's hidden spare area. No OOB upload. */
            result = write_enable();
            if (result == FEL_NAND_OK)
            {
                result = command(tx, sizeof(tx), 0, 0U);
            }
            if (result == FEL_NAND_OK)
            {
                result = row_command(0x10U, address / NAND_PAGE, &status);
                if (result == FEL_NAND_OK && (status & 8U) != 0U)
                {
                    result = FEL_NAND_PROGRAM_FAIL;
                }
            }
        }
        if (result != FEL_NAND_OK)
        {
            return result;
        }
        address += count;
        *completed += count;
    }
    return FEL_NAND_OK;
}

static int execute(const FelNandMailbox *request, uint32_t *detail, uint32_t *completed)
{
    uint32_t operation = request->operation, address = request->address, length = request->length;
    if (operation < FEL_NAND_PROBE || operation > FEL_NAND_BAD_BLOCK)
    {
        return FEL_NAND_OPERATION;
    }
    if ((operation == FEL_NAND_PROBE && (address != 0U || length != 0U)) ||
        (operation == FEL_NAND_BAD_BLOCK &&
         (address >= NAND_SIZE || address % NAND_BLOCK != 0U || length != 0U)) ||
        ((operation == FEL_NAND_READ || operation == FEL_NAND_PROGRAM ||
          operation == FEL_NAND_ERASE) &&
         (address >= NAND_SIZE || length == 0U || length > NAND_SIZE - address)) ||
        ((operation == FEL_NAND_READ || operation == FEL_NAND_PROGRAM) &&
         length > FEL_NAND_BUFFER_SIZE) ||
        (operation == FEL_NAND_ERASE && (address % NAND_BLOCK != 0U || length != NAND_BLOCK)) ||
        ((operation == FEL_NAND_PROGRAM || operation == FEL_NAND_ERASE) &&
         (address >= BOOT_AREA || length > BOOT_AREA - address)) ||
        (operation == FEL_NAND_PROGRAM && (address % NAND_PAGE != 0U || length % NAND_PAGE != 0U ||
                                           (address % NAND_BLOCK) + length > NAND_BLOCK)))
    {
        return FEL_NAND_RANGE;
    }
    uint8_t tx[2] = {0x9FU, 0U}, id[2], status, config, protection = 0U;
    int result = command(tx, sizeof(tx), id, sizeof(id));
    if (result != FEL_NAND_OK)
    {
        return result;
    }
    *detail = ((uint32_t)id[0] << 8) | id[1];
    if (*detail != 0xC212U)
    {
        return FEL_NAND_ID;
    }
    result = wait_ready(&status);
    if (result != FEL_NAND_OK || operation == FEL_NAND_PROBE)
    {
        return result;
    }
    result = get_feature(0xB0U, &config);
    if (result != FEL_NAND_OK)
    {
        return result;
    }
    if ((config & 0x40U) != 0U)
    {
        return FEL_NAND_FEATURE; /* Refuse OTP access mode; never modify OTP. */
    }
    if ((config & 0x10U) == 0U)
    {
        result = set_feature(0xB0U, config | 0x10U);
        if (result != FEL_NAND_OK)
        {
            return result;
        }
    }
    if (operation == FEL_NAND_BAD_BLOCK)
    {
        return bad_block(address, detail);
    }
    int mutating = operation == FEL_NAND_ERASE || operation == FEL_NAND_PROGRAM;
    if (mutating)
    {
        uint32_t markers;
        result = bad_block(address & ~(NAND_BLOCK - 1U), &markers);
        if (result != FEL_NAND_OK)
        {
            return result;
        }
        if (markers != 0xFFFFU)
        {
            *detail = markers;
            return FEL_NAND_BAD;
        }
        result = get_feature(0xA0U, &protection);
        if (result != FEL_NAND_OK)
        {
            return result;
        }
        if ((protection & 0x81U) != 0U)
        {
            return FEL_NAND_PROTECTED; /* Do not bypass hardware/solid lock. */
        }
        /* Power-on BP bits protect the whole chip. Unlock volatile A0 only
         * for this command, then restore its previous value below. */
        if (protection != 0U)
        {
            result = set_feature(0xA0U, 0U);
        }
    }
    if (result == FEL_NAND_OK)
    {
        result = transfer(request, detail, completed);
    }
    if (mutating && protection != 0U)
    {
        int restore = set_feature(0xA0U, protection);
        if (result == FEL_NAND_OK)
        {
            result = restore;
        }
    }
    return result;
}

void fel_nand_dispatch(void)
{
    volatile FelNandMailbox *shared = mailbox();
    FelNandMailbox request = *shared;
    uint32_t detail = 0U, completed = 0U;
    int result = FEL_NAND_ABI;
    if (request.magic == FEL_NAND_MAGIC)
    {
        uint32_t crc = 0xFFFFFFFFU;
        const uint8_t *header = (const uint8_t *)&request;
        for (uint32_t i = 0U; i < 20U; ++i)
        {
            crc = crc_update(crc, header[i]);
        }
        if (request.operation == FEL_NAND_PROGRAM && request.length <= FEL_NAND_BUFFER_SIZE)
        {
            for (uint32_t i = 0U; i < request.length; ++i)
            {
                crc = crc_update(crc, transfer_buffer()[i]);
            }
        }
        result = FEL_NAND_CRC;
        if (~crc == request.crc)
        {
            spi_init();
            result = execute(&request, &detail, &completed);
            spi_stop();
        }
    }
    shared->detail = detail;
    shared->completed = completed;
    shared->status = (uint32_t)result;
}
