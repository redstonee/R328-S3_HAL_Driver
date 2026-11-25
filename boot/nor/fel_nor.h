#ifndef R328_FEL_NOR_H
#define R328_FEL_NOR_H

#define FEL_NOR_MAGIC 0x524F4E46
#define FEL_NOR_ENTRY 0x28000
#define FEL_NOR_MAILBOX 0x29000
#define FEL_NOR_BUFFER 0x2A000
#define FEL_NOR_BUFFER_SIZE 0x4000
#define FEL_NOR_STACK_BOTTOM 0x2F000
#define FEL_NOR_STACK_TOP 0x30000

#ifndef __ASSEMBLER__
#include <stdint.h>

enum
{
    FEL_NOR_PROBE = 1,
    FEL_NOR_READ = 2,
    FEL_NOR_ERASE = 3,
    FEL_NOR_PROGRAM = 4
};
enum
{
    FEL_NOR_OK = 0,
    FEL_NOR_ABI = 1,
    FEL_NOR_OPERATION = 2,
    FEL_NOR_RANGE = 3,
    FEL_NOR_SPI = 4,
    FEL_NOR_BUSY = 5,
    FEL_NOR_ID = 6,
    FEL_NOR_PROTECTED = 7,
    FEL_NOR_CRC = 8,
    FEL_NOR_CPU = 9
};

typedef struct
{
    uint32_t magic, sequence, operation, address, length, crc;
    uint32_t status, detail, completed;
} FelNorMailbox;
_Static_assert(sizeof(FelNorMailbox) == 36U, "FEL NOR mailbox ABI");
#endif

#endif
