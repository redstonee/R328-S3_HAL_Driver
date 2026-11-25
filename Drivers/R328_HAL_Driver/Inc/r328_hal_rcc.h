#ifndef R328_HAL_RCC_H
#define R328_HAL_RCC_H
#include "r328_hal_def.h"

/* This initial profile uses OSC24M directly for APB2 (UART/TWI shared clock). */
HAL_StatusTypeDef HAL_RCC_APB2_Config24M(void);
uint32_t HAL_RCC_GetPCLK2Freq(void);
HAL_StatusTypeDef HAL_RCC_UART_EnableClock(UART_TypeDef *instance);
HAL_StatusTypeDef HAL_RCC_UART_ForceReset(UART_TypeDef *instance);
HAL_StatusTypeDef HAL_RCC_SPI_EnableClock(SPI_TypeDef *instance);
HAL_StatusTypeDef HAL_RCC_SPI_ForceReset(SPI_TypeDef *instance);
HAL_StatusTypeDef HAL_RCC_I2C_EnableClock(I2C_TypeDef *instance);
HAL_StatusTypeDef HAL_RCC_I2C_ForceReset(I2C_TypeDef *instance);
#define RCC_AUDIO_FAMILY_48K 0U
#define RCC_AUDIO_FAMILY_44K1 1U
/* Explicit shared PLL configuration; refuses retuning while audio clocks are enabled. */
HAL_StatusTypeDef HAL_RCC_AudioPLL_Config(uint32_t family);
uint32_t HAL_RCC_GetAudioPLL4XFreq(void);
HAL_StatusTypeDef HAL_RCC_I2S_EnableClock(I2S_TypeDef *instance);
HAL_StatusTypeDef HAL_RCC_I2S_ForceReset(I2S_TypeDef *instance);
uint32_t HAL_RCC_GetI2SClockFreq(I2S_TypeDef *instance);
HAL_StatusTypeDef HAL_RCC_LEDC_EnableClock(LEDC_TypeDef *instance);
HAL_StatusTypeDef HAL_RCC_LEDC_ForceReset(LEDC_TypeDef *instance);
#endif
