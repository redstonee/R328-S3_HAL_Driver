#ifndef BUS_TEST_SETUP_H
#define BUS_TEST_SETUP_H
#include "r328_hal.h"
void Error_Handler(void) __attribute__((noreturn));
extern SPI_HandleTypeDef hspi1;
extern I2C_HandleTypeDef hi2c1;
extern I2S_HandleTypeDef hi2s0;
typedef enum
{
    TEST_SPI_NONE,
    TEST_SPI_LCD,
    TEST_SPI_TOUCH
} Test_SPI_Device;
HAL_StatusTypeDef Test_SPI1_Init(uint32_t hz);
HAL_StatusTypeDef Test_SPI1_Select(Test_SPI_Device device);
HAL_StatusTypeDef Test_I2C1_Init(uint32_t hz);
HAL_StatusTypeDef Test_I2S0_Init(uint32_t rate, uint32_t bits);
#endif
