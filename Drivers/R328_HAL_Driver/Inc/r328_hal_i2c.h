#ifndef R328_HAL_I2C_H
#define R328_HAL_I2C_H
#include "r328_hal_def.h"
#define I2C_ADDRESSINGMODE_7BIT 0U
#define I2C_MEMADD_SIZE_8BIT 1U
#define I2C_MEMADD_SIZE_16BIT 2U
#define HAL_I2C_ERROR_NONE 0U
#define HAL_I2C_ERROR_BERR (1U << 0)
#define HAL_I2C_ERROR_ARLO (1U << 1)
#define HAL_I2C_ERROR_AF (1U << 2)
#define HAL_I2C_ERROR_TIMEOUT (1U << 3)
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
void HAL_I2C_MspDeInit(I2C_HandleTypeDef *handle);
#endif
