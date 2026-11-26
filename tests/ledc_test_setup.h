#ifndef LEDC_TEST_SETUP_H
#define LEDC_TEST_SETUP_H
#include "r328_hal.h"
void Error_Handler(void) __attribute__((noreturn));
extern LEDC_HandleTypeDef hledc;
HAL_StatusTypeDef Test_LEDC_Init(uint32_t count);
#endif
