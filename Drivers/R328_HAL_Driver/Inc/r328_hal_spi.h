#ifndef R328_HAL_SPI_H
#define R328_HAL_SPI_H
#include "r328_hal_def.h"
#include "r328_hal_dma.h"
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
#define SPI_MAX_BAUD_RATE_HZ 100000000U
#define HAL_SPI_ERROR_NONE 0U
#define HAL_SPI_ERROR_FIFO (1U << 0)
#define HAL_SPI_ERROR_TIMEOUT (1U << 1)
#define HAL_SPI_ERROR_DMA (1U << 2)
#define HAL_SPI_ERROR_ABORT (1U << 3)
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
    DMA_HandleTypeDef *hdmatx, *hdmarx;
    /* Private asynchronous state. Keep handles/buffers valid until callback. */
    const uint8_t *TxBuffer;
    uint8_t *RxBuffer;
    uint32_t XferSize, XferStart, XferTimeout;
    volatile uint32_t XferCount;
    uint32_t Offset, BatchSize;
    volatile uint8_t AsyncMode, BusDone, DmaDone;
    uint8_t DmaActive;
} SPI_HandleTypeDef;
/* Module-wide polling deadline, shared by all instances and directions.
 * Default: HAL_POLLING_DEFAULT_TIMEOUT_MS (100 ms). 0 checks readiness without
 * waiting; HAL_MAX_DELAY waits indefinitely. Set in CPU0 foreground; Init and
 * DeInit preserve it. Each polling call snapshots the value once. */
void HAL_SPI_SetPollingTimeout(uint32_t timeout);
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *handle);
/* READY only; deselect all external devices before changing the bus clock. */
HAL_StatusTypeDef HAL_SPI_SetBaudRate(SPI_HandleTypeDef *handle, uint32_t baud_rate_hz);
HAL_StatusTypeDef HAL_SPI_DeInit(SPI_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *handle, const uint8_t *data, uint32_t size);
HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *handle, uint8_t *data, uint32_t size);
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *handle, const uint8_t *tx, uint8_t *rx,
                                          uint32_t size);
/* Foreground APIs. Call Process regularly; callbacks run in Process/Abort. */
HAL_StatusTypeDef HAL_SPI_Transmit_IT(SPI_HandleTypeDef *handle, const uint8_t *data,
                                      uint32_t size);
HAL_StatusTypeDef HAL_SPI_Receive_IT(SPI_HandleTypeDef *handle, uint8_t *data, uint32_t size);
HAL_StatusTypeDef HAL_SPI_TransmitReceive_IT(SPI_HandleTypeDef *handle, const uint8_t *tx,
                                             uint8_t *rx, uint32_t size);
HAL_StatusTypeDef HAL_SPI_Transmit_DMA(SPI_HandleTypeDef *handle, const uint8_t *data,
                                       uint32_t size);
HAL_StatusTypeDef HAL_SPI_Receive_DMA(SPI_HandleTypeDef *handle, uint8_t *data, uint32_t size);
HAL_StatusTypeDef HAL_SPI_TransmitReceive_DMA(SPI_HandleTypeDef *handle, const uint8_t *tx,
                                              uint8_t *rx, uint32_t size);
/* Init sets 100 ms; set while READY. HAL_MAX_DELAY disables the deadline. */
HAL_StatusTypeDef HAL_SPI_SetAsyncTimeout(SPI_HandleTypeDef *handle, uint32_t timeout);
HAL_StatusTypeDef HAL_SPI_Abort(SPI_HandleTypeDef *handle);
void HAL_SPI_Process(SPI_HandleTypeDef *handle);
void HAL_SPI_IRQHandler(SPI_HandleTypeDef *handle);
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *handle);
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *handle);
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *handle);
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *handle);
void HAL_SPI_MspInit(SPI_HandleTypeDef *handle);
void HAL_SPI_MspDeInit(SPI_HandleTypeDef *handle);
#endif
