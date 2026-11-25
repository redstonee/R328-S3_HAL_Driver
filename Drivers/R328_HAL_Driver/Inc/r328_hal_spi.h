#ifndef R328_HAL_SPI_H
#define R328_HAL_SPI_H
#include "r328_hal_def.h"
#define SPI_MODE_MASTER 1U
#define SPI_POLARITY_LOW 0U
#define SPI_POLARITY_HIGH 2U
#define SPI_PHASE_1EDGE 0U
#define SPI_PHASE_2EDGE 1U
#define SPI_FIRSTBIT_MSB 0U
#define SPI_FIRSTBIT_LSB (1U << 12)
#define SPI_NSS_SOFT 0U        /* Application controls a separate GPIO CS. */
#define SPI_NSS_HARD_OUTPUT 1U /* Driver asserts selected peripheral SS per call. */
#define SPI_DATASIZE_8BIT 8U
#define HAL_SPI_ERROR_NONE 0U
#define HAL_SPI_ERROR_FIFO (1U << 0)
#define HAL_SPI_ERROR_TIMEOUT (1U << 1)
typedef enum
{
    HAL_SPI_STATE_RESET,
    HAL_SPI_STATE_READY,
    HAL_SPI_STATE_BUSY
} HAL_SPI_StateTypeDef;
typedef struct
{
    uint32_t Mode, DataSize, CLKPolarity, CLKPhase, NSS, BaudRateHz, FirstBit, ChipSelect;
} SPI_InitTypeDef;
typedef struct
{
    SPI_TypeDef *Instance;
    SPI_InitTypeDef Init;
    volatile HAL_LockTypeDef Lock;
    volatile HAL_SPI_StateTypeDef State;
    volatile uint32_t ErrorCode;
    uint32_t ActualBaudRateHz;
} SPI_HandleTypeDef;
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_SPI_DeInit(SPI_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *handle, const uint8_t *data, uint32_t size,
                                   uint32_t timeout);
HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *handle, uint8_t *data, uint32_t size,
                                  uint32_t timeout);
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *handle, const uint8_t *tx, uint8_t *rx,
                                          uint32_t size, uint32_t timeout);
void HAL_SPI_MspInit(SPI_HandleTypeDef *handle);
void HAL_SPI_MspDeInit(SPI_HandleTypeDef *handle);
#endif
