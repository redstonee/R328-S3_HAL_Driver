#ifndef R328_HAL_H
#define R328_HAL_H
#include "r328_hal_conf.h"
#include "r328_hal_def.h"
#include "r328_hal_rcc.h"
#include "r328_hal_gpio.h"
#include "r328_hal_uart.h"
#include "r328_hal_spi.h"
#include "r328_hal_i2c.h"
#include "r328_hal_i2s.h"
#include "r328_hal_tim.h"
#include "r328_hal_ledc.h"
#include "r328_hal_gic.h"

HAL_StatusTypeDef HAL_Init(void);
/* Establish a boot-relative epoch; counter must already run at 24 MHz.
 * Returns HAL_TIMEOUT if stalled, HAL_BUSY if already initialized. */
HAL_StatusTypeDef HAL_InitTick(void);
/* Milliseconds since HAL_InitTick(), wrapping at 2^32 ms; no timer IRQ required. */
uint32_t HAL_GetTick(void);
/* Kept for source compatibility; no operation with the counter timebase. */
void HAL_IncTick(void);
void HAL_Delay(uint32_t milliseconds);
void HAL_MspInit(void);
#endif
