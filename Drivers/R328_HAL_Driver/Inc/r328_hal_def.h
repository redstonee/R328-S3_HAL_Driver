#ifndef R328_HAL_DEF_H
#define R328_HAL_DEF_H
#include "r328.h"

typedef enum
{
    HAL_OK = 0,
    HAL_ERROR,
    HAL_BUSY,
    HAL_TIMEOUT
} HAL_StatusTypeDef;
typedef enum
{
    HAL_UNLOCKED = 0,
    HAL_LOCKED
} HAL_LockTypeDef;
#define __weak __attribute__((weak))
#define HAL_MAX_DELAY UINT32_MAX
#define UNUSED(x) ((void)(x))

/* Serialized against IRQs on CPU0. Handles must not be shared with CPU1. */
static inline HAL_StatusTypeDef HAL_TryLock(volatile HAL_LockTypeDef *lock)
{
    uint32_t state = R328_IRQ_Save();
    HAL_StatusTypeDef result = HAL_BUSY;
    if (*lock == HAL_UNLOCKED)
    {
        *lock = HAL_LOCKED;
        result = HAL_OK;
    }
    R328_IRQ_Restore(state);
    return result;
}
static inline void HAL_Unlock(volatile HAL_LockTypeDef *lock)
{
    uint32_t state = R328_IRQ_Save();
    *lock = HAL_UNLOCKED;
    R328_IRQ_Restore(state);
}
#endif
