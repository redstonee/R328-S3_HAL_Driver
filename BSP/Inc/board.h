#ifndef BOARD_H
#define BOARD_H
#include "r328_hal.h"

/* Set to 0 when PB6/PB7 are used by another peripheral. */
#ifndef BOARD_LED_ENABLE
#define BOARD_LED_ENABLE 1
#endif
#define BOARD_LED_PORT GPIOB
#define BOARD_LED_BLUE_PIN GPIO_PIN_6 /* LED2, active high. */
#define BOARD_LED_RED_PIN GPIO_PIN_7  /* LED3, active high. */

typedef enum
{
    BOARD_LED_BLUE,
    BOARD_LED_RED,
    BOARD_LED_COUNT
} Board_LED_TypeDef;

extern UART_HandleTypeDef huart0;
extern SPI_HandleTypeDef hspi1;
extern I2C_HandleTypeDef hi2c1;
extern I2S_HandleTypeDef hi2s0;
extern LEDC_HandleTypeDef hledc;
#define BOARD_WS2812_LED_COUNT 4U
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
HAL_StatusTypeDef Board_LEDC_Init(uint32_t led_count);
HAL_StatusTypeDef Board_Init(void);
HAL_StatusTypeDef Board_ConsoleWrite(const char *text);
HAL_StatusTypeDef Board_ConsoleWriteU32(uint32_t value);
void Board_LED_On(Board_LED_TypeDef led);
void Board_LED_Off(Board_LED_TypeDef led);
void Board_LED_Toggle(Board_LED_TypeDef led);
#endif
