#ifndef R328_HAL_BUS_PRIVATE_H
#define R328_HAL_BUS_PRIVATE_H
#include "r328_hal.h"
/* Host doubles model FIFO, W1C and self-clearing registers. Production accesses
 * are direct volatile loads/stores; FIFO byte accesses stay byte accesses. */
#if defined(R328_HAL_HOST_TEST)
uint32_t R328_HAL_TestRead32(volatile uint32_t *reg);
void R328_HAL_TestWrite32(volatile uint32_t *reg, uint32_t value);
uint8_t R328_HAL_TestRead8(volatile uint32_t *reg);
void R328_HAL_TestWrite8(volatile uint32_t *reg, uint8_t value);
#define BUS_READ(reg) R328_HAL_TestRead32(&(reg))
#define BUS_WRITE(reg, value) R328_HAL_TestWrite32(&(reg), (value))
#define BUS_READ8(reg) R328_HAL_TestRead8(&(reg))
#define BUS_WRITE8(reg, value) R328_HAL_TestWrite8(&(reg), (value))
#else
#define BUS_READ(reg) (reg)
#define BUS_WRITE(reg, value) ((reg) = (value))
#define BUS_READ8(reg) (*(volatile uint8_t *)&(reg))
#define BUS_WRITE8(reg, value) (*(volatile uint8_t *)&(reg) = (value))
#endif
static inline int bus_expired(uint32_t start, uint32_t timeout)
{
    return timeout != HAL_MAX_DELAY && (uint32_t)(HAL_GetTick() - start) >= timeout;
}
static inline HAL_StatusTypeDef bus_wait_clear(volatile uint32_t *reg, uint32_t mask)
{
    for (uint32_t count = 0U; count < HAL_REGISTER_POLL_LIMIT; ++count)
    {
        if ((BUS_READ(*reg) & mask) == 0U)
        {
            return HAL_OK;
        }
    }
    return HAL_TIMEOUT;
}
#endif
