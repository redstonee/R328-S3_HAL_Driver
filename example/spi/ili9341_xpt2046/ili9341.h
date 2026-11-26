#ifndef EXAMPLE_ILI9341_H
#define EXAMPLE_ILI9341_H
#include "r328_hal.h"

#define ILI9341_WIDTH 240U
#define ILI9341_HEIGHT 320U
/* Datasheet write limit: 10 MHz. Faster rates require panel/wiring validation. */
#ifndef ILI9341_SPI_HZ
#define ILI9341_SPI_HZ 10000000U
#endif
#define ILI9341_PIXEL_BUFFER_SIZE 256U /* Four hardware FIFOs; no framebuffer. */
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
