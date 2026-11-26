#ifndef R328_HAL_I2C_H
#define R328_HAL_I2C_H
#include "r328_hal_def.h"
#include "r328_hal_dma.h"
#define I2C_ADDRESSINGMODE_7BIT 0U
#define I2C_MEMADD_SIZE_8BIT 1U
#define I2C_MEMADD_SIZE_16BIT 2U
#define HAL_I2C_ERROR_NONE 0U
#define HAL_I2C_ERROR_BERR (1U << 0)
#define HAL_I2C_ERROR_ARLO (1U << 1)
#define HAL_I2C_ERROR_AF (1U << 2)
#define HAL_I2C_ERROR_TIMEOUT (1U << 3)
#define HAL_I2C_ERROR_DMA (1U << 4)
#define HAL_I2C_ERROR_ABORT (1U << 5)
typedef enum
{
    HAL_I2C_STATE_RESET,
    HAL_I2C_STATE_READY,
    HAL_I2C_STATE_BUSY
} HAL_I2C_StateTypeDef;
typedef struct
{
    uint32_t ClockSpeed, AddressingMode;
} I2C_InitTypeDef;
typedef struct
{
    I2C_TypeDef *Instance;
    I2C_InitTypeDef Init;
    volatile HAL_LockTypeDef Lock;
    volatile HAL_I2C_StateTypeDef State;
    volatile uint32_t ErrorCode;
    uint32_t ActualClockSpeed;
    DMA_HandleTypeDef *hdmatx, *hdmarx;
    /* Private asynchronous state; buffers must remain valid until completion. */
    const uint8_t *TxBuffer;
    uint8_t *RxBuffer;
    volatile uint32_t XferCount;
    uint32_t XferSize, XferStart, XferTimeout;
    uint16_t DeviceAddress, MemAddress;
    uint8_t MemSize, MemRemaining, Read, Memory;
    volatile uint8_t AsyncMode, Phase, DmaDone, BusDone;
    uint8_t Expected, StartAddress;
} I2C_HandleTypeDef;
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *handle);
/* STM32 convention: pass the 7-bit address shifted left by one (0x50 << 1). */
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *handle, uint16_t address,
                                          const uint8_t *data, uint32_t size, uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *handle, uint16_t address, uint8_t *data,
                                         uint32_t size, uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *handle, uint16_t address,
                                    uint16_t mem_address, uint32_t mem_size, const uint8_t *data,
                                    uint32_t size, uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *handle, uint16_t address,
                                   uint16_t mem_address, uint32_t mem_size, uint8_t *data,
                                   uint32_t size, uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *handle, uint16_t address,
                                        uint32_t trials, uint32_t timeout);
void HAL_I2C_MspInit(I2C_HandleTypeDef *handle);
/* Nonblocking APIs: call Process regularly for STOP completion and deadlines.
 * I2C callbacks run in Process/Abort context. Call transfer APIs in foreground. */
HAL_StatusTypeDef HAL_I2C_Master_Transmit_IT(I2C_HandleTypeDef *h, uint16_t address,
                                             const uint8_t *data, uint32_t size);
HAL_StatusTypeDef HAL_I2C_Master_Receive_IT(I2C_HandleTypeDef *h, uint16_t address, uint8_t *data,
                                            uint32_t size);
HAL_StatusTypeDef HAL_I2C_Mem_Write_IT(I2C_HandleTypeDef *h, uint16_t address, uint16_t mem,
                                       uint32_t mem_size, const uint8_t *data, uint32_t size);
HAL_StatusTypeDef HAL_I2C_Mem_Read_IT(I2C_HandleTypeDef *h, uint16_t address, uint16_t mem,
                                      uint32_t mem_size, uint8_t *data, uint32_t size);
HAL_StatusTypeDef HAL_I2C_Master_Transmit_DMA(I2C_HandleTypeDef *h, uint16_t address,
                                              const uint8_t *data, uint32_t size);
HAL_StatusTypeDef HAL_I2C_Master_Receive_DMA(I2C_HandleTypeDef *h, uint16_t address, uint8_t *data,
                                             uint32_t size);
HAL_StatusTypeDef HAL_I2C_Mem_Write_DMA(I2C_HandleTypeDef *h, uint16_t address, uint16_t mem,
                                        uint32_t mem_size, const uint8_t *data, uint32_t size);
HAL_StatusTypeDef HAL_I2C_Mem_Read_DMA(I2C_HandleTypeDef *h, uint16_t address, uint16_t mem,
                                       uint32_t mem_size, uint8_t *data, uint32_t size);
/* Set while READY; Init sets 100 ms. HAL_MAX_DELAY disables the deadline. */
HAL_StatusTypeDef HAL_I2C_SetAsyncTimeout(I2C_HandleTypeDef *h, uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Abort(I2C_HandleTypeDef *h);
void HAL_I2C_Process(I2C_HandleTypeDef *h);
void HAL_I2C_IRQHandler(I2C_HandleTypeDef *h);
void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *h);
void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef *h);
void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *h);
void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *h);
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *h);
void HAL_I2C_MspDeInit(I2C_HandleTypeDef *handle);
#endif
