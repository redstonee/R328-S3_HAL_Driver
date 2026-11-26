#include "example.h"
#include "i2c_async_config.h"

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

#define EXAMPLE_TX_DMA_CHANNEL 0U
#define EXAMPLE_RX_DMA_CHANNEL 1U
static DMA_HandleTypeDef tx_dma = {
    .Init = {EXAMPLE_TX_DMA_CHANNEL, DMA_MEMORY_TO_PERIPH, DMA_REQUEST_TWI1}};
static DMA_HandleTypeDef rx_dma = {
    .Init = {EXAMPLE_RX_DMA_CHANNEL, DMA_PERIPH_TO_MEMORY, DMA_REQUEST_TWI1}};

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
    HAL_StatusTypeDef result = Board_I2C1_Init(EXAMPLE_I2C_CLOCK_HZ);
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
    Example_Report("I2C result=", result);
    Example_PrintValue("elapsed ms=", elapsed);
    Example_PrintValue("callback complete=", completed);
    Example_PrintValue("I2C ErrorCode=", hi2c1.ErrorCode);
    if (completed != 0U && (operation == 'r' || operation == 'm'))
    {
        Example_Dump(rx, sizeof(rx));
    }
    completed = failed = 0U;
    operation = 0U;
}
static void start_transfer(uint8_t command)
{
    if (hi2c1.State == HAL_I2C_STATE_BUSY)
    {
        (void)Board_ConsoleWrite("I2C busy; buffers retained. a=abort.\r\n");
        return;
    }
    /* Errors that reset the controller require Init before the next command. */
    if (hi2c1.State == HAL_I2C_STATE_RESET)
    {
        HAL_StatusTypeDef result = bus_init();
        Example_Report("I2C reinit=", result);
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
        result = HAL_I2C_Master_Transmit_DMA(&hi2c1, address, tx, sizeof(tx));
        break;
    case 'r':
        result = HAL_I2C_Master_Receive_DMA(&hi2c1, address, rx, sizeof(rx));
        break;
    case 'w':
        result = HAL_I2C_Mem_Write_DMA(&hi2c1, address, EXAMPLE_I2C_REGISTER,
                                       EXAMPLE_I2C_REGISTER_SIZE, tx, sizeof(tx));
        break;
    default: /* m: register address followed by repeated START and read. */
        result = HAL_I2C_Mem_Read_DMA(&hi2c1, address, EXAMPLE_I2C_REGISTER,
                                      EXAMPLE_I2C_REGISTER_SIZE, rx, sizeof(rx));
        break;
    }
    Example_Report("I2C start=", result);
    if (result != HAL_OK)
    {
        Example_PrintValue("I2C ErrorCode=", hi2c1.ErrorCode);
        if (hi2c1.State != HAL_I2C_STATE_BUSY)
        {
            operation = 0U; /* A rejected start usually has no callback. */
        }
    }
}
int main(void)
{
    Example_Init("I2C1 / DMA / generic master and register transfers");
    Example_RequireOK("I2C init=", bus_init());
    Example_RequireOK("TX DMA init=", HAL_DMA_Init(&tx_dma));
    Example_RequireOK("RX DMA init=", HAL_DMA_Init(&rx_dma));
    hi2c1.hdmatx = &tx_dma;
    hi2c1.hdmarx = &rx_dma;
    Example_PrintValue("I2C actual Hz=", hi2c1.ActualClockSpeed);
    Example_PrintValue("7-bit address=", EXAMPLE_I2C_ADDRESS);
    (void)Board_ConsoleWrite(
        "Set i2c_async_config.h for your device.\r\n"
        "t=master TX, r=master RX, m=register read, w=register write, a=abort.\r\n"
        "No automatic transfer; t/w send the configured payload.\r\n");
    for (;;)
    {
        /* Keep servicing deadlines/STOP completion while doing foreground work.
         * The HAL installs GIC handlers; callbacks run in Process/Abort. */
        HAL_I2C_Process(&hi2c1);
        report_completion();
        uint8_t command;
        if (HAL_UART_Receive(&huart0, &command, 1U, 0U) != HAL_OK)
        {
            continue;
        }
        if (command == 'a')
        {
            Example_Report("I2C abort=", HAL_I2C_Abort(&hi2c1));
        }
        else if (command == 't' || command == 'r' || command == 'm' || command == 'w')
        {
            start_transfer(command);
        }
    }
}
