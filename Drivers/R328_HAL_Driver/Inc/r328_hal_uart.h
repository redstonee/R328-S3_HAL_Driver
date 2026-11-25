#ifndef R328_HAL_UART_H
#define R328_HAL_UART_H
#include "r328_hal_def.h"

#define UART_WORDLENGTH_7B 2U
#define UART_WORDLENGTH_8B 3U
#define UART_STOPBITS_1 0U
#define UART_STOPBITS_2 (1U << 2)
#define UART_PARITY_NONE 0U
#define UART_PARITY_ODD (1U << 3)
#define UART_PARITY_EVEN ((1U << 3) | (1U << 4))
#define HAL_UART_ERROR_NONE 0U
#define HAL_UART_ERROR_ORE (1U << 1)
#define HAL_UART_ERROR_PE (1U << 2)
#define HAL_UART_ERROR_FE (1U << 3)
#define HAL_UART_ERROR_BI (1U << 4)
typedef enum
{
    HAL_UART_STATE_RESET = 0,
    HAL_UART_STATE_READY,
    HAL_UART_STATE_BUSY_TX,
    HAL_UART_STATE_BUSY_RX
} HAL_UART_StateTypeDef;
typedef struct
{
    uint32_t BaudRate;
    uint32_t WordLength;
    uint32_t StopBits;
    uint32_t Parity;
} UART_InitTypeDef;
typedef struct
{
    UART_TypeDef *Instance;
    UART_InitTypeDef Init;
    volatile HAL_LockTypeDef Lock;
    volatile HAL_UART_StateTypeDef State;
    volatile uint32_t ErrorCode;
} UART_HandleTypeDef;

HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_UART_DeInit(UART_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *handle, const uint8_t *data, uint32_t size,
                                    uint32_t timeout);
HAL_StatusTypeDef HAL_UART_Receive(UART_HandleTypeDef *handle, uint8_t *data, uint32_t size,
                                   uint32_t timeout);
void HAL_UART_MspInit(UART_HandleTypeDef *handle);
void HAL_UART_MspDeInit(UART_HandleTypeDef *handle);
#endif
