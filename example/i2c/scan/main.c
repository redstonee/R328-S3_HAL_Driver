#include "example.h"

static void scan(void)
{
    uint32_t found = 0U;
    (void)Board_ConsoleWrite("Scanning 7-bit addresses 0x08..0x77...\r\n");
    for (uint32_t address = 0x08U; address <= 0x77U; ++address)
    {
        HAL_StatusTypeDef status = HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(address << 1), 1U, 20U);
        if (status == HAL_OK)
        {
            ++found;
            (void)Board_ConsoleWrite("ACK 0x");
            uint8_t byte = (uint8_t)address;
            Example_Dump(&byte, 1U);
        }
        else if (status != HAL_ERROR || hi2c1.ErrorCode != HAL_I2C_ERROR_AF)
        {
            Example_Report("Scan stopped=", status);
            Example_PrintValue("I2C ErrorCode=", hi2c1.ErrorCode);
            /* A timeout/bus error leaves RESET; explicitly restore the controller. */
            if (hi2c1.State == HAL_I2C_STATE_RESET)
            {
                Example_RequireOK("I2C reinit=", Board_I2C1_Init(100000U));
            }
            return;
        }
    }
    Example_PrintValue("devices found=", found);
}

int main(void)
{
    Example_Init("I2C1 / address scanner");
    Example_RequireOK("I2C init=", Board_I2C1_Init(100000U));
    scan();
    (void)Board_ConsoleWrite("Press s to scan again.\r\n");
    for (;;)
    {
        uint8_t byte;
        if (HAL_UART_Receive(&huart0, &byte, 1U, 0U) == HAL_OK && byte == 's')
        {
            scan();
        }
    }
}
