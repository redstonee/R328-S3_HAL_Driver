#ifndef MAIN_H
#define MAIN_H
#include "r328_hal.h"
void SystemClock_Config(void);
void Error_Handler(void) __attribute__((noreturn));

#define LED1_PORT GPIOB
#define LED1_PIN GPIO_PIN_6

#define LED2_PORT GPIOB
#define LED2_PIN GPIO_PIN_7

#define UART0_PORT GPIOH
#define UART0_TX_PIN GPIO_PIN_0
#define UART0_RX_PIN GPIO_PIN_1

#endif
