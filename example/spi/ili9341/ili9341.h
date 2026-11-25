#ifndef EXAMPLE_ILI9341_H
#define EXAMPLE_ILI9341_H
#include "board.h"

#define ILI9341_WIDTH 240U
#define ILI9341_HEIGHT 320U
/* Both devices use mode 0 at 2 MHz: no speed changes while sharing SPI1. */
#define DISPLAY_SPI_HZ 2000000U
#define ILI9341_BLACK 0x0000U
#define ILI9341_WHITE 0xFFFFU
#define ILI9341_RED 0xF800U
#define ILI9341_GREEN 0x07E0U
#define ILI9341_BLUE 0x001FU

HAL_StatusTypeDef ILI9341_Init(void);
HAL_StatusTypeDef ILI9341_FillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                                   uint16_t rgb565);
HAL_StatusTypeDef ILI9341_Fill(uint16_t rgb565);
void ILI9341_Backlight(uint32_t enabled);
#endif
