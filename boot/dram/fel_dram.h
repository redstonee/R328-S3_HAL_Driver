#ifndef FEL_DRAM_H
#define FEL_DRAM_H

/* Keep all temporary storage inside FEL's previously verified 64 KiB window.
 * Only the CPU overwrites low SRAM, while USB is suspended during exec.
 */
#define FEL_DRAM_VENDOR_STAGE 0x28000
#define FEL_DRAM_VENDOR_SIZE 0x3100
#define FEL_DRAM_ENTRY 0x2c000
#define FEL_DRAM_MAILBOX 0x2cf00
#define FEL_DRAM_BACKUP 0x2d000
#define FEL_DRAM_STACK_BOTTOM 0x35000
#define FEL_DRAM_STACK_TOP 0x38000
#define FEL_DRAM_BROM_STACK_MIN 0x38000
#define FEL_DRAM_VENDOR_BASE 0x20000
#define FEL_DRAM_BACKUP_SIZE 0x8000
#define FEL_DRAM_VENDOR_BSS 0x27b44
#define FEL_DRAM_VENDOR_BSS_END 0x27dec
#define FEL_DRAM_VENDOR_INIT 0x22dad /* Thumb init_DRAM(0, parameters). */
#define FEL_DRAM_PARAMETERS 0x20038
#define FEL_DRAM_MAGIC 0x52444446
#define FEL_DRAM_ABI 1
#define FEL_DRAM_RUNNING 1
#define FEL_DRAM_OK 2
#define FEL_DRAM_INIT_FAILED 3
#define FEL_DRAM_CPU_ERROR 4
#define FEL_DRAM_STACK_ERROR 5
#define FEL_DRAM_STACK_CANARY 0x53a7c19e

#endif
