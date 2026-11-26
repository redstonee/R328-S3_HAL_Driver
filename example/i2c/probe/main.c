#include "main.h"

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
        (void)HAL_UART_PrintValue(&huart0, label, (uint32_t)status, UART_TIMEOUT_MS);
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
    (void)HAL_UART_PrintString(&huart0, "\r\nR328 example: ", UART_TIMEOUT_MS);
    (void)HAL_UART_PrintString(&huart0, banner, UART_TIMEOUT_MS);
    (void)HAL_UART_PrintString(&huart0, "\r\nstatus: 0=OK 1=ERROR 2=BUSY 3=TIMEOUT\r\n",
                               UART_TIMEOUT_MS);
}

static void uart_poll(void)
{
    uint8_t byte;
    if (HAL_UART_Receive(&huart0, &byte, 1U, 0U) == HAL_OK)
    {
        (void)HAL_UART_Transmit(&huart0, &byte, 1U, UART_TIMEOUT_MS);
    }
}

static void idle(void) __attribute__((noreturn));
static void idle(void)
{
    /* Polling RX needs the CPU; the architecture counter does not wake WFI. */
    for (;;)
    {
        uart_poll();
    }
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

int main(void)
{
    app_init("I2C1 / address probe");
    HAL_StatusTypeDef status = i2c_init(100000U);
    HAL_UART_PrintValue(&huart0, "I2C1 init=", status, UART_TIMEOUT_MS);
    if (status != HAL_OK)
    {
        Error_Handler();
    }
    /* Address probe sends only address+W and STOP, without register data. */
    status = HAL_I2C_IsDeviceReady(&hi2c1, 0x50U << 1, 1U, 20U);
    HAL_UART_PrintValue(&huart0, "Probe 0x50 (NACK expected if absent)=", status, UART_TIMEOUT_MS);
    (void)HAL_UART_PrintString(&huart0, "I2C ErrorCode=", UART_TIMEOUT_MS);
    (void)HAL_UART_PrintU32(&huart0, hi2c1.ErrorCode, UART_TIMEOUT_MS);
    (void)HAL_UART_PrintString(&huart0, "\r\nUART echo active.\r\n", UART_TIMEOUT_MS);
    idle();
}
