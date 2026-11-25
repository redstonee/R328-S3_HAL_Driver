#include "example.h"

/* Deliberately exceeds the 64-byte FIFO, exercising multiple batches. */
#define TRANSFER_SIZE 131U
static uint8_t tx[TRANSFER_SIZE], rx[TRANSFER_SIZE];

int main(void)
{
    Example_Init("SPI1 / external MOSI-MISO loopback / 131 bytes");
    Example_RequireOK("SPI init=", Board_SPI1_Init(1000000U));
    Example_RequireOK("SPI deselect=", Board_SPI1_Select(BOARD_SPI_NONE));
    (void)Board_ConsoleWrite("Connect MOSI to MISO; leave both device selects high.\r\n");
    uint32_t round = 0U;
    for (;;)
    {
        for (uint32_t i = 0U; i < TRANSFER_SIZE; ++i)
        {
            tx[i] = (uint8_t)((i + round) ^ 0xA5U);
        }
        Example_RequireOK("SPI transfer=",
                          HAL_SPI_TransmitReceive(&hspi1, tx, rx, sizeof(tx), 100U));
        uint32_t mismatches = 0U;
        for (uint32_t i = 0U; i < TRANSFER_SIZE; ++i)
        {
            if (tx[i] != rx[i])
            {
                ++mismatches;
            }
        }
        Example_PrintValue("round=", round++);
        Example_PrintValue("mismatched bytes=", mismatches);
        (void)Board_ConsoleWrite("RX first 16 bytes: ");
        Example_Dump(rx, 16U);
        HAL_Delay(1000U);
    }
}
