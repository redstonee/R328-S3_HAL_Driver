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

static SPI_HandleTypeDef hspi1;
static HAL_StatusTypeDef spi_init(uint32_t baud_rate_hz)
{
    if (hspi1.State == HAL_SPI_STATE_BUSY)
    {
        return HAL_BUSY;
    }
    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_4 | GPIO_PIN_8, GPIO_PIN_SET);
    GPIO_InitTypeDef pins = {.Pin = GPIO_PIN_4 | GPIO_PIN_8,
                             .Mode = GPIO_MODE_OUTPUT_PP,
                             .Pull = GPIO_PULLUP,
                             .Drive = GPIO_DRIVE_LEVEL1};
    HAL_StatusTypeDef status = HAL_GPIO_Init(GPIOH, &pins);
    if (status != HAL_OK)
    {
        return status;
    }
    pins = (GPIO_InitTypeDef){.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7,
                              .Mode = GPIO_MODE_AF_PP,
                              .Pull = GPIO_NOPULL,
                              .Drive = GPIO_DRIVE_LEVEL1,
                              .Alternate = 3U};
    status = HAL_GPIO_Init(GPIOH, &pins);
    if (status != HAL_OK)
    {
        return status;
    }
    hspi1.Instance = SPI1;
    hspi1.Init = (SPI_InitTypeDef){.Mode = SPI_MODE_MASTER,
                                   .DataSize = SPI_DATASIZE_8BIT,
                                   .CLKPolarity = SPI_POLARITY_LOW,
                                   .CLKPhase = SPI_PHASE_1EDGE,
                                   .NSS = SPI_NSS_SOFT,
                                   .BaudRateHz = baud_rate_hz,
                                   .FirstBit = SPI_FIRSTBIT_MSB,
                                   .ChipSelect = 0U};
    return HAL_SPI_Init(&hspi1);
}
static HAL_StatusTypeDef spi_deselect(void)
{
    if (hspi1.State == HAL_SPI_STATE_BUSY)
    {
        return HAL_BUSY;
    }
    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_4 | GPIO_PIN_8, GPIO_PIN_SET);
    return HAL_OK;
}

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
    HAL_StatusTypeDef result = spi_init(EXAMPLE_SPI_CLOCK_HZ);
    if (result == HAL_OK)
    {
        result = spi_deselect();
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
    HAL_UART_PrintValue(&huart0, "SPI result=", result);
    HAL_UART_PrintValue(&huart0, "elapsed ms=", elapsed);
    HAL_UART_PrintValue(&huart0, "callback complete=", completed);
    HAL_UART_PrintValue(&huart0, "SPI ErrorCode=", hspi1.ErrorCode);
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
        HAL_UART_PrintValue(&huart0, "mismatched bytes=", mismatches);
        (void)HAL_UART_PrintString(&huart0,
                                   mismatches == 0U ? "Loopback PASS\r\n" : "Loopback FAIL\r\n");
        HAL_UART_HexDump(&huart0, rx, EXAMPLE_SPI_PREVIEW_SIZE);
    }
    completed = failed = 0U;
    operation = 0U;
}
static void start_transfer(uint8_t command)
{
    if (hspi1.State == HAL_SPI_STATE_BUSY)
    {
        (void)HAL_UART_PrintString(&huart0, "SPI busy; buffers retained. a=abort.\r\n");
        return;
    }
    if (hspi1.State == HAL_SPI_STATE_RESET)
    {
        HAL_StatusTypeDef result = bus_init();
        HAL_UART_PrintValue(&huart0, "SPI reinit=", result);
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
    HAL_UART_PrintValue(&huart0, "SPI start=", result);
    if (result != HAL_OK)
    {
        HAL_UART_PrintValue(&huart0, "SPI ErrorCode=", hspi1.ErrorCode);
        if (hspi1.State != HAL_SPI_STATE_BUSY)
        {
            operation = 0U;
        }
        /* If DMA cleanup retains BUSY, Process keeps ownership of these buffers. */
    }
}
int main(void)
{
    app_init("SPI1 / DMA / external MOSI-MISO loopback");
    require_ok("SPI init=", bus_init());
    require_ok("TX DMA init=", HAL_DMA_Init(&tx_dma));
    require_ok("RX DMA init=", HAL_DMA_Init(&rx_dma));
    hspi1.hdmatx = &tx_dma;
    hspi1.hdmarx = &rx_dma;
    HAL_UART_PrintValue(&huart0, "SPI actual Hz=", hspi1.ActualBaudRateHz);
    (void)HAL_UART_PrintString(&huart0,
                               "Connect PH6 MOSI to PH7 MISO; both device selects stay high.\r\n"
                               "t=TX only, r=RX dummy 0xFF, x=full-duplex loopback, a=abort.\r\n");
    for (;;)
    {
        HAL_SPI_Process(&hspi1);
        report_completion();
        uint8_t command;
        if (HAL_UART_GetRxFifoLevel(&huart0) == 0U ||
            HAL_UART_Receive(&huart0, &command, 1U) != HAL_OK)
        {
            continue;
        }
        if (command == 'a')
        {
            HAL_UART_PrintValue(&huart0, "SPI abort=", HAL_SPI_Abort(&hspi1));
        }
        else if (command == 't' || command == 'r' || command == 'x')
        {
            start_transfer(command);
        }
    }
}
