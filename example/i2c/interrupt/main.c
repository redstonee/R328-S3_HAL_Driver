#include "main.h"
#include "i2c_async_config.h"

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

_Static_assert(EXAMPLE_I2C_ADDRESS >= 0x08U && EXAMPLE_I2C_ADDRESS <= 0x77U,
               "Expected a non-reserved 7-bit address");
_Static_assert(EXAMPLE_I2C_READ_SIZE > 0U && EXAMPLE_I2C_READ_SIZE <= 64U, "Read buffer size");
_Static_assert(EXAMPLE_I2C_REGISTER_SIZE == I2C_MEMADD_SIZE_8BIT ||
                   EXAMPLE_I2C_REGISTER_SIZE == I2C_MEMADD_SIZE_16BIT,
               "Expected an 8-bit or 16-bit register address");
_Static_assert(EXAMPLE_I2C_REGISTER <=
                   (EXAMPLE_I2C_REGISTER_SIZE == I2C_MEMADD_SIZE_8BIT ? 0xFFU : 0xFFFFU),
               "Register address does not fit");

/* Static writable buffers survive until the final completion/error callback. */
static uint8_t tx[] = {EXAMPLE_I2C_TX_DATA};
static uint8_t rx[EXAMPLE_I2C_READ_SIZE];
_Static_assert(sizeof(tx) > 0U && sizeof(tx) <= 64U, "Write buffer size");
static volatile uint32_t completed, failed;
static uint32_t started;
static uint8_t operation;

void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *h)
{
    if (h == &hi2c1)
    {
        completed = 1U;
    }
}
void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef *h)
{
    if (h == &hi2c1)
    {
        completed = 1U;
    }
}
void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *h)
{
    if (h == &hi2c1)
    {
        completed = 1U;
    }
}
void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *h)
{
    if (h == &hi2c1)
    {
        completed = 1U;
    }
}
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *h)
{
    if (h == &hi2c1)
    {
        failed = 1U;
    }
}
static HAL_StatusTypeDef bus_init(void)
{
    HAL_StatusTypeDef result = i2c_init(EXAMPLE_I2C_CLOCK_HZ);
    return result == HAL_OK ? HAL_I2C_SetAsyncTimeout(&hi2c1, EXAMPLE_I2C_TIMEOUT_MS) : result;
}
static void report_completion(void)
{
    if (completed == 0U && failed == 0U)
    {
        return;
    }
    uint32_t elapsed = HAL_GetTick() - started;
    HAL_StatusTypeDef result = failed == 0U                                      ? HAL_OK
                               : (hi2c1.ErrorCode & HAL_I2C_ERROR_TIMEOUT) != 0U ? HAL_TIMEOUT
                                                                                 : HAL_ERROR;
    HAL_UART_PrintValue(&huart0, "I2C result=", result);
    HAL_UART_PrintValue(&huart0, "elapsed ms=", elapsed);
    HAL_UART_PrintValue(&huart0, "callback complete=", completed);
    HAL_UART_PrintValue(&huart0, "I2C ErrorCode=", hi2c1.ErrorCode);
    if (completed != 0U && (operation == 'r' || operation == 'm'))
    {
        HAL_UART_HexDump(&huart0, rx, sizeof(rx));
    }
    completed = failed = 0U;
    operation = 0U;
}
static void start_transfer(uint8_t command)
{
    if (hi2c1.State == HAL_I2C_STATE_BUSY)
    {
        (void)HAL_UART_PrintString(&huart0, "I2C busy; buffers retained. a=abort.\r\n");
        return;
    }
    /* Errors that reset the controller require Init before the next command. */
    if (hi2c1.State == HAL_I2C_STATE_RESET)
    {
        HAL_StatusTypeDef result = bus_init();
        HAL_UART_PrintValue(&huart0, "I2C reinit=", result);
        if (result != HAL_OK)
        {
            return;
        }
    }
    completed = failed = 0U;
    operation = command;
    started = HAL_GetTick();
    const uint16_t address = EXAMPLE_I2C_ADDRESS << 1;
    HAL_StatusTypeDef result;
    switch (command)
    {
    case 't':
        result = HAL_I2C_Master_Transmit_IT(&hi2c1, address, tx, sizeof(tx));
        break;
    case 'r':
        result = HAL_I2C_Master_Receive_IT(&hi2c1, address, rx, sizeof(rx));
        break;
    case 'w':
        result = HAL_I2C_Mem_Write_IT(&hi2c1, address, EXAMPLE_I2C_REGISTER,
                                      EXAMPLE_I2C_REGISTER_SIZE, tx, sizeof(tx));
        break;
    default: /* m: register address followed by repeated START and read. */
        result = HAL_I2C_Mem_Read_IT(&hi2c1, address, EXAMPLE_I2C_REGISTER,
                                     EXAMPLE_I2C_REGISTER_SIZE, rx, sizeof(rx));
        break;
    }
    HAL_UART_PrintValue(&huart0, "I2C start=", result);
    if (result != HAL_OK)
    {
        HAL_UART_PrintValue(&huart0, "I2C ErrorCode=", hi2c1.ErrorCode);
        if (hi2c1.State != HAL_I2C_STATE_BUSY)
        {
            operation = 0U; /* A rejected start usually has no callback. */
        }
    }
}
int main(void)
{
    app_init("I2C1 / interrupt / generic master and register transfers");
    require_ok("I2C init=", bus_init());
    HAL_UART_PrintValue(&huart0, "I2C actual Hz=", hi2c1.ActualClockSpeed);
    HAL_UART_PrintValue(&huart0, "7-bit address=", EXAMPLE_I2C_ADDRESS);
    (void)HAL_UART_PrintString(
        &huart0, "Set i2c_async_config.h for your device.\r\n"
                 "t=master TX, r=master RX, m=register read, w=register write, a=abort.\r\n"
                 "No automatic transfer; t/w send the configured payload.\r\n");
    for (;;)
    {
        /* Keep servicing deadlines/STOP completion while doing foreground work.
         * The HAL installs GIC handlers; callbacks run in Process/Abort. */
        HAL_I2C_Process(&hi2c1);
        report_completion();
        uint8_t command;
        if (HAL_UART_GetRxFifoLevel(&huart0) == 0U ||
            HAL_UART_Receive(&huart0, &command, 1U) != HAL_OK)
        {
            continue;
        }
        if (command == 'a')
        {
            HAL_UART_PrintValue(&huart0, "I2C abort=", HAL_I2C_Abort(&hi2c1));
        }
        else if (command == 't' || command == 'r' || command == 'm' || command == 'w')
        {
            start_transfer(command);
        }
    }
}
