#ifndef R328_FEL_NAND_H
#define R328_FEL_NAND_H

#define FEL_NAND_MAGIC 0x444E4146
#define FEL_NAND_ENTRY 0x28000
#define FEL_NAND_MAILBOX 0x29000
#define FEL_NAND_BUFFER 0x2A000
#define FEL_NAND_BUFFER_SIZE 0x4000
#define FEL_NAND_STACK_BOTTOM 0x2F000
#define FEL_NAND_STACK_TOP 0x30000

#ifndef __ASSEMBLER__
#include <stdint.h>

enum
{
    FEL_NAND_PROBE = 1,
    FEL_NAND_READ = 2,
    FEL_NAND_ERASE = 3,
    FEL_NAND_PROGRAM = 4,
    FEL_NAND_BAD_BLOCK = 5
};
enum
{
    FEL_NAND_OK = 0,
    FEL_NAND_ABI = 1,
    FEL_NAND_OPERATION = 2,
    FEL_NAND_RANGE = 3,
    FEL_NAND_SPI = 4,
    FEL_NAND_BUSY = 5,
    FEL_NAND_ID = 6,
    FEL_NAND_PROTECTED = 7,
    FEL_NAND_CRC = 8,
    FEL_NAND_CPU = 9,
    FEL_NAND_ECC = 10,
    FEL_NAND_BAD = 11,
    FEL_NAND_ERASE_FAIL = 12,
    FEL_NAND_PROGRAM_FAIL = 13,
    FEL_NAND_FEATURE = 14
};

typedef struct
{
    uint32_t magic, sequence, operation, address, length, crc;
    uint32_t status, detail, completed;
} FelNandMailbox;
_Static_assert(sizeof(FelNandMailbox) == 36U, "FEL NAND mailbox ABI");
#endif

#endif
