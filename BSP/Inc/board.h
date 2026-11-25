#ifndef BOARD_H
#define BOARD_H
#include "r328_hal.h"

/* Enable only after checking the board schematic and selecting an unused GPIO. */
#ifndef BOARD_LED_ENABLE
#define BOARD_LED_ENABLE 1
#endif
#if BOARD_LED_ENABLE
#define BOARD_LED_PORT GPIOB
#define BOARD_LED_PIN GPIO_PIN_6
#endif

extern UART_HandleTypeDef huart0;
extern SPI_HandleTypeDef hspi1;
extern I2C_HandleTypeDef hi2c1;
extern I2S_HandleTypeDef hi2s0;
typedef enum
{
    BOARD_SPI_NONE,
    BOARD_SPI_LCD,
    BOARD_SPI_TOUCH
} Board_SPI_Device;
HAL_StatusTypeDef Board_SPI1_Init(uint32_t baud_rate_hz);
HAL_StatusTypeDef Board_SPI1_Select(Board_SPI_Device device);
HAL_StatusTypeDef Board_I2C1_Init(uint32_t clock_speed);
HAL_StatusTypeDef Board_I2S0_Init(uint32_t sample_rate, uint32_t sample_bits);
HAL_StatusTypeDef Board_Init(void);
HAL_StatusTypeDef Board_ConsoleWrite(const char *text);
HAL_StatusTypeDef Board_ConsoleWriteU32(uint32_t value);
void Board_LED_Toggle(void);
#endif
