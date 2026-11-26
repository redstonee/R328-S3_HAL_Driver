#ifndef EXAMPLE_DISPLAY_BUS_H
#define EXAMPLE_DISPLAY_BUS_H
#include "r328_hal.h"

extern SPI_HandleTypeDef hspi1;
typedef enum
{
    DISPLAY_SPI_NONE,
    DISPLAY_SPI_LCD,
    DISPLAY_SPI_TOUCH
} Display_SPI_Device;

typedef enum
{
    DISPLAY_SPI_POLLING,
    DISPLAY_SPI_INTERRUPT,
    DISPLAY_SPI_DMA
} Display_SPI_Mode;

HAL_StatusTypeDef Display_SPI_Init(void);
HAL_StatusTypeDef Display_SPI_SetMode(Display_SPI_Mode mode);
/* Switch rate with both CS inactive, then select the requested device. */
HAL_StatusTypeDef Display_SPI_Select(Display_SPI_Device device);
/* Device helpers wait for the selected HAL mode to finish before returning.
 * CS and D/C remain owned by the device driver throughout that wait. */
HAL_StatusTypeDef Display_SPI_Transmit(const uint8_t *data, uint32_t size, uint32_t timeout);
HAL_StatusTypeDef Display_SPI_TransmitReceive(const uint8_t *tx, uint8_t *rx, uint32_t size,
                                              uint32_t timeout);
#endif
