#ifndef R328_IT_H
#define R328_IT_H
#include <stdint.h>
typedef struct
{
    uint32_t Magic;
    uint32_t Exception;
    uint32_t PC;
    uint32_t CPSR;
    uint32_t DFSR;
    uint32_t DFAR;
    uint32_t IFSR;
    uint32_t IFAR;
} R328_FaultRecord;
extern volatile R328_FaultRecord g_fault_record;
extern volatile uint32_t g_unhandled_irq;
void IRQ_Handler(void);
void Fault_Handler(uint32_t exception, uint32_t pc, uint32_t cpsr) __attribute__((noreturn));
#endif
