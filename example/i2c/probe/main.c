#include "example.h"

int main(void)
{
    Example_Init("I2C1 / address probe");
    HAL_StatusTypeDef status = Board_I2C1_Init(100000U);
    Example_Report("I2C1 init=", status);
    if (status != HAL_OK)
    {
        Error_Handler();
    }
    /* Address probe sends only address+W and STOP, without register data. */
    status = HAL_I2C_IsDeviceReady(&hi2c1, 0x50U << 1, 1U, 20U);
    Example_Report("Probe 0x50 (NACK expected if absent)=", status);
    (void)Board_ConsoleWrite("I2C ErrorCode=");
    (void)Board_ConsoleWriteU32(hi2c1.ErrorCode);
    (void)Board_ConsoleWrite("\r\nUART echo active.\r\n");
    Example_Idle();
}
