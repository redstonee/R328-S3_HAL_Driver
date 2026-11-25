#ifndef R328_HAL_I2S_H
#define R328_HAL_I2S_H
#include "r328_hal_def.h"
#define I2S_MODE_MASTER_TX 0U
#define I2S_MODE_MASTER_RX 1U
#define I2S_MODE_MASTER_TX_RX 2U
#define I2S_MODE_SLAVE_TX 3U
#define I2S_MODE_SLAVE_RX 4U
#define I2S_MODE_SLAVE_TX_RX 5U
#define I2S_STANDARD_PHILIPS 0U
#define I2S_STANDARD_MSB 1U
#define I2S_DATAFORMAT_16B 16U
#define I2S_DATAFORMAT_24B 24U
#define I2S_DATAFORMAT_32B 32U
#define I2S_MCLKOUTPUT_DISABLE 0U
#define I2S_MCLKOUTPUT_ENABLE 1U
#define HAL_I2S_ERROR_NONE 0U
#define HAL_I2S_ERROR_UDR (1U << 6)
#define HAL_I2S_ERROR_OVR (1U << 1)
#define HAL_I2S_ERROR_FIFO ((1U << 5) | (1U << 2))
#define HAL_I2S_ERROR_TIMEOUT (1U << 8)
typedef enum
{
    HAL_I2S_STATE_RESET,
    HAL_I2S_STATE_READY,
    HAL_I2S_STATE_BUSY
} HAL_I2S_StateTypeDef;
typedef struct
{
    uint32_t Mode, Standard, DataFormat, SlotWidth, AudioFreq, MCLKOutput, MCLKMultiple;
} I2S_InitTypeDef;
typedef struct
{
    I2S_TypeDef *Instance;
    I2S_InitTypeDef Init;
    volatile HAL_LockTypeDef Lock;
    volatile HAL_I2S_StateTypeDef State;
    volatile uint32_t ErrorCode;
    volatile uint32_t TxXferCount, RxXferCount;
} I2S_HandleTypeDef;
HAL_StatusTypeDef HAL_I2S_Init(I2S_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_I2S_DeInit(I2S_HandleTypeDef *handle);
/* Size is sample count, even for stereo L,R. Each sample occupies a uint32_t,
 * with 16/24/32 valid bits at the LSB. Receive sign-extends to 32 bits.
 * Finite blocking transfers stop the interface before returning. */
HAL_StatusTypeDef HAL_I2S_Transmit(I2S_HandleTypeDef *handle, const uint32_t *data, uint32_t size,
                                   uint32_t timeout);
HAL_StatusTypeDef HAL_I2S_Receive(I2S_HandleTypeDef *handle, uint32_t *data, uint32_t size,
                                  uint32_t timeout);
HAL_StatusTypeDef HAL_I2S_TransmitReceive(I2S_HandleTypeDef *handle, const uint32_t *tx,
                                          uint32_t *rx, uint32_t size, uint32_t timeout);
void HAL_I2S_MspInit(I2S_HandleTypeDef *handle);
void HAL_I2S_MspDeInit(I2S_HandleTypeDef *handle);
#endif
