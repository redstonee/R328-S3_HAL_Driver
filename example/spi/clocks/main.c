#include "example.h"

int main(void)
{
    Example_Init("SPI1 / unselected clock test");
    HAL_StatusTypeDef status = Board_SPI1_Init(1000000U);
    Example_Report("SPI1 init=", status);
    if (status != HAL_OK)
    {
        Error_Handler();
    }
    (void)Board_ConsoleWrite("Both CS high; inspect PH5/PH6. MISO data is undefined.\r\n");
    for (;;)
    {
        uint8_t tx[] = {0x55U, 0xAAU, 0x00U, 0xFFU}, rx[4];
        /* No LCD, touch or Flash command is issued. Test data equality only
         * with an external MOSI/MISO loopback and other devices disconnected. */
        status = Board_SPI1_Select(BOARD_SPI_NONE);
        if (status == HAL_OK)
        {
            status = HAL_SPI_TransmitReceive(&hspi1, tx, rx, sizeof(tx), 100U);
        }
        Example_Report("SPI1 clocks=", status);
        if (status != HAL_OK)
        {
            Error_Handler();
        }
        HAL_Delay(1000U);
    }
}
