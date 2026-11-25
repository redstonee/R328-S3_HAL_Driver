/* Callable SRAM helper for stock xfel read/write/exec. */
#include "fel_nor.h"

#include "../common/fel_spi.h"

#ifdef R328_NOR_HOST_TEST
extern volatile FelNorMailbox nor_test_mailbox;
#define mailbox() (&nor_test_mailbox)
#define transfer_buffer() nor_test_buffer()
#else
#define mailbox() ((volatile FelNorMailbox *)(uintptr_t)FEL_NOR_MAILBOX)
#define transfer_buffer() ((uint8_t *)(uintptr_t)FEL_NOR_BUFFER)

#endif

static int status_register(uint8_t opcode, uint8_t *status)
{
    return command(&opcode, 1U, status, 1U);
}

static int wait_ready(uint32_t milliseconds)
{
    uint32_t start = counter_now();
    for (uint32_t i = 0U; i < POLL_LIMIT; ++i)
    {
        uint8_t status;
        int result = status_register(0x05U, &status);
        if (result != FEL_NOR_OK)
        {
            return result;
        }
        if ((status & 1U) == 0U)
        {
            return FEL_NOR_OK;
        }
        /* Low CNTPCT wraps at ~179 s; all command timeouts are <=3 s. */
        if (counter_now() - start >= milliseconds * 24000U)
        {
            break;
        }
    }
    return FEL_NOR_BUSY;
}

static int write_enable(void)
{
    uint8_t opcode = 0x06U, status;
    int result = command(&opcode, 1U, 0, 0U);
    if (result == FEL_NOR_OK)
    {
        result = status_register(0x05U, &status);
        if (result == FEL_NOR_OK && (status & 2U) == 0U)
        {
            result = FEL_NOR_PROTECTED;
        }
    }
    return result;
}

static int program(uint32_t address, uint32_t length, uint32_t *completed)
{
    uint8_t tx[260];
    uint8_t *buffer = transfer_buffer();
    for (uint32_t offset = 0U; offset < length;)
    {
        uint32_t count = 256U - (address & 255U);
        if (count > length - offset)
        {
            count = length - offset;
        }
        tx[0] = 0x02U;
        tx[1] = (uint8_t)(address >> 16);
        tx[2] = (uint8_t)(address >> 8);
        tx[3] = (uint8_t)address;
        for (uint32_t i = 0U; i < count; ++i)
        {
            tx[4U + i] = buffer[offset + i];
        }
        int result = write_enable();
        if (result == FEL_NOR_OK)
        {
            result = command(tx, count + 4U, 0, 0U);
        }
        if (result == FEL_NOR_OK)
        {
            result = wait_ready(20U);
        }
        if (result != FEL_NOR_OK)
        {
            return result;
        }
        address += count;
        offset += count;
        *completed = offset;
    }
    return FEL_NOR_OK;
}

static int execute(const FelNorMailbox *request, uint32_t *detail, uint32_t *completed)
{
    uint32_t operation = request->operation;
    if (operation < FEL_NOR_PROBE || operation > FEL_NOR_PROGRAM)
    {
        return FEL_NOR_OPERATION;
    }
    uint32_t address = request->address, length = request->length;
    if ((operation == FEL_NOR_PROBE && (address != 0U || length != 0U)) ||
        (operation != FEL_NOR_PROBE &&
         (address >= NOR_SIZE || length == 0U || length > NOR_SIZE - address)) ||
        ((operation == FEL_NOR_READ || operation == FEL_NOR_PROGRAM) &&
         length > FEL_NOR_BUFFER_SIZE) ||
        (operation == FEL_NOR_ERASE && ((address & 0xFFFFU) != 0U || length != 0x10000U)))
    {
        return FEL_NOR_RANGE;
    }
    uint8_t tx[4] = {0x9FU, 0U, 0U, 0U}, id[3];
    int result = command(tx, 1U, id, 3U);
    if (result != FEL_NOR_OK)
    {
        return result;
    }
    *detail = ((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | id[2];
    if (*detail != 0xEF4017U && *detail != 0xEF7017U)
    {
        return FEL_NOR_ID;
    }
    result = wait_ready(3000U);
    if (result != FEL_NOR_OK || operation == FEL_NOR_PROBE)
    {
        return result;
    }
    if (operation == FEL_NOR_READ)
    {
        result = nor_read(address, transfer_buffer(), length) == 0 ? FEL_NOR_OK : FEL_NOR_SPI;
    }
    else
    {
        uint8_t status1, status2;
        result = status_register(0x05U, &status1);
        if (result == FEL_NOR_OK)
        {
            result = status_register(0x35U, &status2);
        }
        if (result == FEL_NOR_OK && ((status1 & 0x7CU) != 0U || (status2 & 0x40U) != 0U))
        {
            return FEL_NOR_PROTECTED; /* Do not change protection / QE registers. */
        }
        if (result != FEL_NOR_OK)
        {
            return result;
        }
        if (operation == FEL_NOR_PROGRAM)
        {
            return program(address, length, completed);
        }
        result = write_enable();
        tx[0] = 0xD8U;
        tx[1] = (uint8_t)(address >> 16);
        tx[2] = (uint8_t)(address >> 8);
        tx[3] = (uint8_t)address;
        if (result == FEL_NOR_OK)
        {
            result = command(tx, 4U, 0, 0U);
        }
        if (result == FEL_NOR_OK)
        {
            result = wait_ready(3000U);
        }
    }
    if (result == FEL_NOR_OK)
    {
        *completed = length;
    }
    return result;
}

void fel_nor_dispatch(void)
{
    volatile FelNorMailbox *shared = mailbox();
    FelNorMailbox request = *shared;
    uint32_t detail = 0U, completed = 0U;
    int result = FEL_NOR_ABI;
    if (request.magic == FEL_NOR_MAGIC)
    {
        uint32_t crc = 0xFFFFFFFFU;
        const uint8_t *header = (const uint8_t *)&request;
        for (uint32_t i = 0U; i < 20U; ++i)
        {
            crc = crc_update(crc, header[i]);
        }
        if (request.operation == FEL_NOR_PROGRAM && request.length <= FEL_NOR_BUFFER_SIZE)
        {
            for (uint32_t i = 0U; i < request.length; ++i)
            {
                crc = crc_update(crc, transfer_buffer()[i]);
            }
        }
        result = FEL_NOR_CRC;
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
