#ifndef R328_HAL_LEDC_H
#define R328_HAL_LEDC_H
#include "r328_hal_def.h"

#define LEDC_MAX_LEDS 1024U
#define HAL_LEDC_ERROR_NONE 0U
#define HAL_LEDC_ERROR_FIFO (1U << 0)
#define HAL_LEDC_ERROR_UNDERRUN (1U << 1)
#define HAL_LEDC_ERROR_TIMEOUT (1U << 2)
#define HAL_LEDC_ERROR_RESET (1U << 3)

typedef enum
{
    HAL_LEDC_STATE_RESET,
    HAL_LEDC_STATE_READY,
    HAL_LEDC_STATE_BUSY
} HAL_LEDC_StateTypeDef;

typedef struct
{
    uint32_t LEDCount;
    /* Pulse widths in OSC24M cycles: T0H/T1L 2..32, T0L/T1H 2..64.
     * Register encodings are cycles minus one (zero has special meaning). */
    uint32_t T0H, T0L, T1H, T1L;
    uint32_t ResetTimeUs, WaitDataTimeUs; /* 1..341 us at 24 MHz. */
} LEDC_InitTypeDef;

typedef struct
{
    LEDC_TypeDef *Instance;
    LEDC_InitTypeDef Init;
    volatile HAL_LockTypeDef Lock;
    volatile HAL_LEDC_StateTypeDef State;
    volatile uint32_t ErrorCode;
} LEDC_HandleTypeDef;

/* FIFO words use 0x00GGRRBB, MSB first. This helper accepts ordinary RGB. */
static inline uint32_t HAL_LEDC_ColorRGB(uint8_t red, uint8_t green, uint8_t blue)
{
    return ((uint32_t)green << 16) | ((uint32_t)red << 8) | blue;
}

HAL_StatusTypeDef HAL_LEDC_Init(LEDC_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_LEDC_DeInit(LEDC_HandleTypeDef *handle);
/* One complete frame: count must equal LEDCount, timeout covers the frame and
 * a final reset/latch pulse. Errors abort and require HAL_LEDC_Init(). */
HAL_StatusTypeDef HAL_LEDC_Transmit(LEDC_HandleTypeDef *handle, const uint32_t *colors,
                                    uint32_t count, uint32_t timeout);
void HAL_LEDC_MspInit(LEDC_HandleTypeDef *handle);
void HAL_LEDC_MspDeInit(LEDC_HandleTypeDef *handle);
#endif
