#ifndef R328_HAL_TIM_H
#define R328_HAL_TIM_H
#include "r328_hal_def.h"

#define TIM_PRESCALER_1 0U
#define TIM_PRESCALER_2 1U
#define TIM_PRESCALER_4 2U
#define TIM_PRESCALER_8 3U
#define TIM_PRESCALER_16 4U
#define TIM_PRESCALER_32 5U
#define TIM_PRESCALER_64 6U
#define TIM_PRESCALER_128 7U
#define TIM_MODE_CONTINUOUS 0U
#define TIM_MODE_ONESHOT 1U
typedef enum
{
    HAL_TIM_STATE_RESET = 0,
    HAL_TIM_STATE_READY,
    HAL_TIM_STATE_BUSY
} HAL_TIM_StateTypeDef;
typedef struct
{
    uint32_t Prescaler; /* Encoded /1../128, not an STM32 PSC register value. */
    uint32_t Period;    /* Raw interval ticks, not ARR-1. */
    uint32_t Mode;
} TIM_Base_InitTypeDef;
typedef struct TIM_HandleTypeDef TIM_HandleTypeDef;
struct TIM_HandleTypeDef
{
    TIM_TypeDef *Instance;
    TIM_Base_InitTypeDef Init;
    volatile HAL_TIM_StateTypeDef State;
    volatile HAL_LockTypeDef Lock;
    void (*PeriodElapsedCallback)(TIM_HandleTypeDef *handle);
};
HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_TIM_Base_DeInit(TIM_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_TIM_Base_Stop(TIM_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_TIM_Base_Stop_IT(TIM_HandleTypeDef *handle);
void HAL_TIM_IRQHandler(TIM_HandleTypeDef *handle);
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *handle);
#endif
