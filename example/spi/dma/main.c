#include "example.h"

#define EXAMPLE_SPI_CLOCK_HZ 1000000U
#define EXAMPLE_SPI_TIMEOUT_MS 100U
#define EXAMPLE_SPI_TRANSFER_SIZE 131U /* Crosses the 64-byte FIFO, with a short tail. */
#define EXAMPLE_SPI_PREVIEW_SIZE 16U
#define EXAMPLE_SPI_PATTERN_XOR 0xA5U
#define EXAMPLE_SPI_DUMMY_BYTE 0xFFU

/* TX and RX must be separate, writable SRAM/DDR objects for DMA. */
static uint8_t tx[EXAMPLE_SPI_TRANSFER_SIZE], rx[EXAMPLE_SPI_TRANSFER_SIZE];
static volatile uint32_t completed, failed;
static uint32_t started, round;
static uint8_t operation;

#define EXAMPLE_TX_DMA_CHANNEL 0U
#define EXAMPLE_RX_DMA_CHANNEL 1U
static DMA_HandleTypeDef tx_dma = {
    .Init = {EXAMPLE_TX_DMA_CHANNEL, DMA_MEMORY_TO_PERIPH, DMA_REQUEST_SPI1}};
static DMA_HandleTypeDef rx_dma = {
    .Init = {EXAMPLE_RX_DMA_CHANNEL, DMA_PERIPH_TO_MEMORY, DMA_REQUEST_SPI1}};

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *h)
{
    if (h == &hspi1)
    {
        completed = 1U;
    }
}
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *h)
{
    if (h == &hspi1)
    {
        completed = 1U;
    }
}
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *h)
{
    if (h == &hspi1)
    {
        completed = 1U;
    }
}
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *h)
{
    if (h == &hspi1)
    {
        failed = 1U;
    }
}
static HAL_StatusTypeDef bus_init(void)
{
    HAL_StatusTypeDef result = Board_SPI1_Init(EXAMPLE_SPI_CLOCK_HZ);
    if (result == HAL_OK)
    {
        result = Board_SPI1_Select(BOARD_SPI_NONE);
    }
    return result == HAL_OK ? HAL_SPI_SetAsyncTimeout(&hspi1, EXAMPLE_SPI_TIMEOUT_MS) : result;
}
static void report_completion(void)
{
    if (completed == 0U && failed == 0U)
    {
        return;
    }
    uint32_t elapsed = HAL_GetTick() - started;
    HAL_StatusTypeDef result = failed == 0U                                      ? HAL_OK
                               : (hspi1.ErrorCode & HAL_SPI_ERROR_TIMEOUT) != 0U ? HAL_TIMEOUT
                                                                                 : HAL_ERROR;
    Example_Report("SPI result=", result);
    Example_PrintValue("elapsed ms=", elapsed);
    Example_PrintValue("callback complete=", completed);
    Example_PrintValue("SPI ErrorCode=", hspi1.ErrorCode);
    if (completed != 0U && (operation == 'r' || operation == 'x'))
    {
        uint32_t mismatches = 0U;
        for (uint32_t i = 0U; i < sizeof(rx); ++i)
        {
            uint8_t expected = operation == 'x' ? tx[i] : EXAMPLE_SPI_DUMMY_BYTE;
            if (rx[i] != expected)
            {
                ++mismatches;
            }
        }
        Example_PrintValue("mismatched bytes=", mismatches);
        (void)Board_ConsoleWrite(mismatches == 0U ? "Loopback PASS\r\n" : "Loopback FAIL\r\n");
        Example_Dump(rx, EXAMPLE_SPI_PREVIEW_SIZE);
    }
    completed = failed = 0U;
    operation = 0U;
}
static void start_transfer(uint8_t command)
{
    if (hspi1.State == HAL_SPI_STATE_BUSY)
    {
        (void)Board_ConsoleWrite("SPI busy; buffers retained. a=abort.\r\n");
        return;
    }
    if (hspi1.State == HAL_SPI_STATE_RESET)
    {
        HAL_StatusTypeDef result = bus_init();
        Example_Report("SPI reinit=", result);
        if (result != HAL_OK)
        {
            return;
        }
    }
    for (uint32_t i = 0U; i < sizeof(tx); ++i)
    {
        tx[i] = (uint8_t)((i + round) ^ EXAMPLE_SPI_PATTERN_XOR);
        rx[i] = 0U;
    }
    ++round;
    operation = command;
    completed = failed = 0U;
    started = HAL_GetTick();
    HAL_StatusTypeDef result;
    if (command == 't')
    {
        result = HAL_SPI_Transmit_DMA(&hspi1, tx, sizeof(tx));
    }
    else if (command == 'r')
    {
        result = HAL_SPI_Receive_DMA(&hspi1, rx, sizeof(rx));
    }
    else
    {
        result = HAL_SPI_TransmitReceive_DMA(&hspi1, tx, rx, sizeof(tx));
    }
    Example_Report("SPI start=", result);
    if (result != HAL_OK)
    {
        Example_PrintValue("SPI ErrorCode=", hspi1.ErrorCode);
        if (hspi1.State != HAL_SPI_STATE_BUSY)
        {
            operation = 0U;
        }
        /* If DMA cleanup retains BUSY, Process keeps ownership of these buffers. */
    }
}
int main(void)
{
    Example_Init("SPI1 / DMA / external MOSI-MISO loopback");
    Example_RequireOK("SPI init=", bus_init());
    Example_RequireOK("TX DMA init=", HAL_DMA_Init(&tx_dma));
    Example_RequireOK("RX DMA init=", HAL_DMA_Init(&rx_dma));
    hspi1.hdmatx = &tx_dma;
    hspi1.hdmarx = &rx_dma;
    Example_PrintValue("SPI actual Hz=", hspi1.ActualBaudRateHz);
    (void)Board_ConsoleWrite("Connect PH6 MOSI to PH7 MISO; both device selects stay high.\r\n"
                             "t=TX only, r=RX dummy 0xFF, x=full-duplex loopback, a=abort.\r\n");
    for (;;)
    {
        HAL_SPI_Process(&hspi1);
        report_completion();
        uint8_t command;
        if (HAL_UART_Receive(&huart0, &command, 1U, 0U) != HAL_OK)
        {
            continue;
        }
        if (command == 'a')
        {
            Example_Report("SPI abort=", HAL_SPI_Abort(&hspi1));
        }
        else if (command == 't' || command == 'r' || command == 'x')
        {
            start_transfer(command);
        }
    }
}
