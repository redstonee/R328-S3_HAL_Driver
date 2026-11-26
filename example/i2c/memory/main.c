#include "main.h"
#include "config.h"

#define UART_TIMEOUT_MS 100U
static UART_HandleTypeDef huart0;
volatile uint32_t g_application_error;

void Error_Handler(void)
{
    g_application_error = 1U;
    R328_IRQ_Disable();
    for (;;)
    {
        R328_WFI();
    }
}
void SystemClock_Config(void)
{
    if (HAL_RCC_APB2_Config24M() != HAL_OK)
    {
        Error_Handler();
    }
    SystemCoreClockUpdate();
}
static void require_ok(const char *label, HAL_StatusTypeDef status)
{
    if (status != HAL_OK)
    {
        (void)HAL_UART_PrintValue(&huart0, label, (uint32_t)status);
        Error_Handler();
    }
}
static void app_init(const char *banner)
{
    if (HAL_Init() != HAL_OK)
    {
        Error_Handler();
    }
    SystemClock_Config();
    HAL_UART_SetPollingTimeout(UART_TIMEOUT_MS);
    /* This example explicitly chooses UART0 PH0/PH1, 115200 8N1 for diagnostics.
     * The framework itself does not reserve any UART or initialize these pins. */
    GPIO_InitTypeDef uart_pins = {.Pin = GPIO_PIN_0 | GPIO_PIN_1,
                                  .Mode = GPIO_MODE_AF_PP,
                                  .Pull = GPIO_PULLUP,
                                  .Drive = GPIO_DRIVE_LEVEL1,
                                  .Alternate = 3U};
    if (HAL_GPIO_Init(GPIOH, &uart_pins) != HAL_OK)
    {
        Error_Handler();
    }
    huart0.Instance = UART0;
    huart0.Init =
        (UART_InitTypeDef){115200U, UART_WORDLENGTH_8B, UART_STOPBITS_1, UART_PARITY_NONE};
    require_ok("UART init=", HAL_UART_Init(&huart0));
    (void)HAL_UART_PrintString(&huart0, "\r\nR328 example: ");
    (void)HAL_UART_PrintString(&huart0, banner);
    (void)HAL_UART_PrintString(&huart0, "\r\nstatus: 0=OK 1=ERROR 2=BUSY 3=TIMEOUT\r\n");
}

static I2C_HandleTypeDef hi2c1;
static HAL_StatusTypeDef i2c_init(uint32_t clock_speed)
{
    if (hi2c1.State == HAL_I2C_STATE_BUSY)
    {
        return HAL_BUSY;
    }
    /* PB8 SCL / PB9 SDA, AF4. The TWI function controls open-drain output. */
    GPIO_InitTypeDef pins = {.Pin = GPIO_PIN_8 | GPIO_PIN_9,
                             .Mode = GPIO_MODE_AF_PP,
                             .Pull = GPIO_PULLUP,
                             .Drive = GPIO_DRIVE_LEVEL1,
                             .Alternate = 4U};
    HAL_StatusTypeDef status = HAL_GPIO_Init(GPIOB, &pins);
    if (status != HAL_OK)
    {
        return status;
    }
    hi2c1.Instance = I2C1;
    hi2c1.Init =
        (I2C_InitTypeDef){.ClockSpeed = clock_speed, .AddressingMode = I2C_ADDRESSINGMODE_7BIT};
    return HAL_I2C_Init(&hi2c1);
}

_Static_assert(EXAMPLE_I2C_ADDRESS <= 0x7FU, "Expected a 7-bit device address");
_Static_assert(EXAMPLE_I2C_READ_SIZE > 0U && EXAMPLE_I2C_READ_SIZE <= 64U, "Read buffer size");

static void read_registers(void)
{
    uint8_t bytes[EXAMPLE_I2C_READ_SIZE];
    HAL_StatusTypeDef status =
        HAL_I2C_Mem_Read(&hi2c1, EXAMPLE_I2C_ADDRESS << 1, EXAMPLE_I2C_REGISTER,
                         EXAMPLE_I2C_REGISTER_SIZE, bytes, sizeof(bytes));
    HAL_UART_PrintValue(&huart0, "register read=", status);
    if (status == HAL_OK)
    {
        HAL_UART_HexDump(&huart0, bytes, sizeof(bytes));
    }
    else
    {
        HAL_UART_PrintValue(&huart0, "I2C ErrorCode=", hi2c1.ErrorCode);
        if (hi2c1.State == HAL_I2C_STATE_RESET)
        {
            require_ok("I2C reinit=", i2c_init(100000U));
        }
    }
}

int main(void)
{
    app_init("I2C1 / register read with repeated START");
    HAL_I2C_SetPollingTimeout(100U);
    require_ok("I2C init=", i2c_init(100000U));
    (void)HAL_UART_PrintString(&huart0,
                               "Set config.h for the target. Press r to read registers.\r\n");
    for (;;)
    {
        uint8_t byte;
        if (HAL_UART_GetRxFifoLevel(&huart0) != 0U &&
            HAL_UART_Receive(&huart0, &byte, 1U) == HAL_OK && byte == 'r')
        {
            read_registers();
        }
    }
}
