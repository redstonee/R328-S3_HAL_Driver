#include "example.h"
#include "config.h"

_Static_assert(EXAMPLE_I2C_ADDRESS <= 0x7FU, "Expected a 7-bit device address");
_Static_assert(EXAMPLE_I2C_READ_SIZE > 0U && EXAMPLE_I2C_READ_SIZE <= 64U, "Read buffer size");

static void read_registers(void)
{
    uint8_t bytes[EXAMPLE_I2C_READ_SIZE];
    HAL_StatusTypeDef status =
        HAL_I2C_Mem_Read(&hi2c1, EXAMPLE_I2C_ADDRESS << 1, EXAMPLE_I2C_REGISTER,
                         EXAMPLE_I2C_REGISTER_SIZE, bytes, sizeof(bytes), 100U);
    Example_Report("register read=", status);
    if (status == HAL_OK)
    {
        Example_Dump(bytes, sizeof(bytes));
    }
    else
    {
        Example_PrintValue("I2C ErrorCode=", hi2c1.ErrorCode);
        if (hi2c1.State == HAL_I2C_STATE_RESET)
        {
            Example_RequireOK("I2C reinit=", Board_I2C1_Init(100000U));
        }
    }
}

int main(void)
{
    Example_Init("I2C1 / register read with repeated START");
    Example_RequireOK("I2C init=", Board_I2C1_Init(100000U));
    (void)Board_ConsoleWrite("Set config.h for the target. Press r to read registers.\r\n");
    for (;;)
    {
        uint8_t byte;
        if (HAL_UART_Receive(&huart0, &byte, 1U, 0U) == HAL_OK && byte == 'r')
        {
            read_registers();
        }
    }
}
