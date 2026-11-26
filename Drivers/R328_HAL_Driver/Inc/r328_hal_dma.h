#ifndef R328_HAL_DMA_H
#define R328_HAL_DMA_H
#include "r328_hal_def.h"

#define DMA_MEMORY_TO_PERIPH 0U
#define DMA_PERIPH_TO_MEMORY 1U
#define DMA_REQUEST_SPI0 22U
#define DMA_REQUEST_SPI1 23U
#define DMA_REQUEST_TWI0 45U
#define DMA_REQUEST_TWI1 46U
#define HAL_DMA_ERROR_NONE 0U
#define HAL_DMA_ERROR_TRANSFER 1U
#define HAL_DMA_ERROR_TIMEOUT 2U
typedef enum
{
    HAL_DMA_STATE_RESET,
    HAL_DMA_STATE_READY,
    HAL_DMA_STATE_BUSY,
    HAL_DMA_STATE_ERROR
} HAL_DMA_StateTypeDef;
typedef struct
{
    uint32_t Channel, Direction, Request;
} DMA_InitTypeDef;
typedef struct __DMA_HandleTypeDef
{
    DMA_InitTypeDef Init;
    volatile HAL_DMA_StateTypeDef State;
    volatile uint32_t ErrorCode;
    void *Parent;
    void (*XferCpltCallback)(struct __DMA_HandleTypeDef *handle);
    /* One six-word descriptor: byte width, single beat, normal (non-circular). */
    uint32_t Descriptor[6] __attribute__((aligned(8)));
} DMA_HandleTypeDef;
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_DMA_DeInit(DMA_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_DMA_Start_IT(DMA_HandleTypeDef *handle, uintptr_t source,
                                   uintptr_t destination, uint32_t size);
HAL_StatusTypeDef HAL_DMA_Abort(DMA_HandleTypeDef *handle);
void HAL_DMA_IRQHandler(DMA_HandleTypeDef *handle);
#endif
