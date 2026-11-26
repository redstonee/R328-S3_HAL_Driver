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

int main(void)
{
    app_init("SPI1 / unselected clock test");
    HAL_SPI_SetPollingTimeout(100U);
    HAL_StatusTypeDef status = spi_init(1000000U);
    HAL_UART_PrintValue(&huart0, "SPI1 init=", status);
    if (status != HAL_OK)
    {
        Error_Handler();
    }
    (void)HAL_UART_PrintString(&huart0,
                               "Both CS high; inspect PH5/PH6. MISO data is undefined.\r\n");
    for (;;)
    {
        uint8_t tx[] = {0x55U, 0xAAU, 0x00U, 0xFFU}, rx[4];
        /* No LCD, touch or Flash command is issued. Test data equality only
         * with an external MOSI/MISO loopback and other devices disconnected. */
        status = spi_deselect();
        if (status == HAL_OK)
        {
            status = HAL_SPI_TransmitReceive(&hspi1, tx, rx, sizeof(tx));
        }
        HAL_UART_PrintValue(&huart0, "SPI1 clocks=", status);
        if (status != HAL_OK)
        {
            Error_Handler();
        }
        HAL_Delay(1000U);
    }
}
