#ifndef R328_EXAMPLE_H
#define R328_EXAMPLE_H
#include "main.h"
#include "board.h"

void Example_Init(const char *banner);
void Example_Report(const char *label, HAL_StatusTypeDef status);
void Example_RequireOK(const char *label, HAL_StatusTypeDef status);
void Example_PrintValue(const char *label, uint32_t value);
void Example_PrintHex(uint32_t value);
void Example_Dump(const uint8_t *data, uint32_t size);
void Example_UART_Poll(void);
void Example_Idle(void) __attribute__((noreturn));
#endif
