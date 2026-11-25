#ifndef R328_HAL_GIC_H
#define R328_HAL_GIC_H
#include "r328_hal_def.h"

typedef void (*HAL_GIC_HandlerTypeDef)(void *context);
/* SVC, CPU0, exclusive GIC ownership; no nested interrupts.
   NONSECURE builds require the secure loader to assign relevant IRQs to Group 1. */
HAL_StatusTypeDef HAL_GIC_Init(void);
HAL_StatusTypeDef HAL_GIC_RegisterIRQ(IRQn_Type irq, HAL_GIC_HandlerTypeDef handler, void *context,
                                      uint8_t priority);
HAL_StatusTypeDef HAL_GIC_UnregisterIRQ(IRQn_Type irq, void *context);
HAL_StatusTypeDef HAL_GIC_EnableIRQ(IRQn_Type irq);
void HAL_GIC_DisableIRQ(IRQn_Type irq);
void HAL_GIC_IRQHandler(void);
void HAL_GIC_UnhandledIRQ(uint32_t irq);
#endif
