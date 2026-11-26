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

/* Deliberately exceeds the 64-byte FIFO, exercising multiple batches. */
#define TRANSFER_SIZE 131U
static uint8_t tx[TRANSFER_SIZE], rx[TRANSFER_SIZE];

int main(void)
{
    app_init("SPI1 / external MOSI-MISO loopback / 131 bytes");
    require_ok("SPI init=", spi_init(1000000U));
    require_ok("SPI deselect=", spi_deselect());
    (void)HAL_UART_PrintString(&huart0, "Connect MOSI to MISO; leave both device selects high.\r\n",
                               UART_TIMEOUT_MS);
    uint32_t round = 0U;
    for (;;)
    {
        for (uint32_t i = 0U; i < TRANSFER_SIZE; ++i)
        {
            tx[i] = (uint8_t)((i + round) ^ 0xA5U);
        }
        require_ok("SPI transfer=", HAL_SPI_TransmitReceive(&hspi1, tx, rx, sizeof(tx), 100U));
        uint32_t mismatches = 0U;
        for (uint32_t i = 0U; i < TRANSFER_SIZE; ++i)
        {
            if (tx[i] != rx[i])
            {
                ++mismatches;
            }
        }
        HAL_UART_PrintValue(&huart0, "round=", round++, UART_TIMEOUT_MS);
        HAL_UART_PrintValue(&huart0, "mismatched bytes=", mismatches, UART_TIMEOUT_MS);
        (void)HAL_UART_PrintString(&huart0, "RX first 16 bytes: ", UART_TIMEOUT_MS);
        HAL_UART_HexDump(&huart0, rx, 16U, UART_TIMEOUT_MS);
        HAL_Delay(1000U);
    }
}
