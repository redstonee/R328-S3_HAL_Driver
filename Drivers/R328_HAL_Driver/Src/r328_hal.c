#include "r328_hal.h"

/* Published once with CPU0 IRQs masked, then immutable for ISR-safe reads. */
static uint64_t tick_epoch;
static uint32_t tick_initialized;

_Static_assert(R328_ARCH_COUNTER_HZ % 1000U == 0U,
               "Counter must provide whole cycles per millisecond");

HAL_StatusTypeDef HAL_Init(void)
{
    R328_IRQ_Disable();
    HAL_MspInit();
    HAL_StatusTypeDef status = HAL_GIC_Init();
    if (status != HAL_OK)
    {
        return status;
    }
    status = HAL_InitTick();
    if (status == HAL_OK)
    {
        R328_IRQ_Enable();
    }
    return status;
}
HAL_StatusTypeDef HAL_InitTick(void)
{
    uint32_t state = R328_IRQ_Save();
    if (tick_initialized != 0U)
    {
        R328_IRQ_Restore(state);
        return HAL_BUSY;
    }
    /* The BROM/loader must leave the system counter enabled and accessible.
     * Do not reset the shared counter or write secure-only CNTFRQ from NS SVC.
     * Check progress without a timebase to avoid an infinite wait at startup. */
    uint64_t start = R328_ArchCounter_Read();
    for (uint32_t remaining = HAL_REGISTER_POLL_LIMIT; remaining != 0U; --remaining)
    {
        if (R328_ArchCounter_Read() != start)
        {
            tick_epoch = start;
            tick_initialized = 1U;
            R328_IRQ_Restore(state);
            return HAL_OK;
        }
    }
    R328_IRQ_Restore(state);
    return HAL_TIMEOUT;
}
__weak uint32_t HAL_GetTick(void)
{
    if (tick_initialized == 0U)
    {
        return 0U;
    }
    uint64_t elapsed = R328_ArchCounter_Read() - tick_epoch;
    /* Divide before narrowing: the low counter word wraps about every 179 s. */
    return (uint32_t)(elapsed / (R328_ARCH_COUNTER_HZ / 1000U));
}
/* Compatibility entry point; hardware now advances time without IRQ callbacks. */
void HAL_IncTick(void) {}
void HAL_Delay(uint32_t milliseconds)
{
    if (milliseconds == 0U)
    {
        return;
    }
    uint32_t start = HAL_GetTick();
    /* Busy wait permits immediate completion when the tick changes. Foreground only. */
    while ((uint32_t)(HAL_GetTick() - start) < milliseconds)
    {
    }
}
__weak void HAL_MspInit(void) {}
