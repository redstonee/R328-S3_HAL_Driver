#include "main.h"

#define UART_TIMEOUT_MS 100U
#define UART_BLOCK_BYTES 8U
static UART_HandleTypeDef huart0;
static uint8_t rx_blocks[2][UART_BLOCK_BYTES];
static volatile uint32_t rx_ready, tx_ready, uart_error;
static uint32_t receive_index;

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

/* HAL callbacks run in foreground Process/Abort, after the handle unlocks. */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *handle)
{
    if (handle == &huart0)
    {
        tx_ready = 1U;
    }
}
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *handle)
{
    if (handle == &huart0)
    {
        rx_ready = 1U;
    }
}
void HAL_UART_ErrorCallback(UART_HandleTypeDef *handle)
{
    if (handle == &huart0)
    {
        uart_error |= handle->ErrorCode;
    }
}

int main(void)
{
    app_init("UART0 IT full-duplex fixed-size echo / 115200 8N1");
    (void)HAL_UART_PrintString(
        &huart0, "Send exactly 8 bytes (e.g. 12345678), then wait for the raw echo.\r\n"
                 "No newline/idle detection; disable terminal local echo.\r\n");
    /* Interactive RX can take arbitrarily long; use application deadlines when
     * needed. The HAL default deadline would otherwise expire after 100 ms. */
    require_ok("UART timeout=", HAL_UART_SetAsyncTimeout(&huart0, HAL_MAX_DELAY));
    require_ok("RX start=", HAL_UART_Receive_IT(&huart0, rx_blocks[0], UART_BLOCK_BYTES));
    static const uint8_t ready[] = "\r\nReady.\r\n";
    require_ok("TX start=", HAL_UART_Transmit_IT(&huart0, ready, sizeof(ready) - 1U));
    for (;;)
    {
        HAL_UART_Process(&huart0);
        if (uart_error != HAL_UART_ERROR_NONE)
        {
            uint32_t saved_error = uart_error;
            /* Stop DMA before releasing either buffer, then report via polling. */
            if (HAL_UART_Abort(&huart0) != HAL_OK)
            {
                Error_Handler();
            }
            (void)HAL_UART_PrintValue(&huart0, "UART ErrorCode=", saved_error);
            Error_Handler();
        }
        if (rx_ready != 0U && tx_ready != 0U)
        {
            uint8_t *completed = rx_blocks[receive_index];
            receive_index ^= 1U;
            rx_ready = 0U;
            tx_ready = 0U;
            /* Receive the next block into a different buffer while the previous
             * block transmits. Neither buffer is reused before its callback. */
            require_ok("RX start=",
                       HAL_UART_Receive_IT(&huart0, rx_blocks[receive_index], UART_BLOCK_BYTES));
            require_ok("TX start=", HAL_UART_Transmit_IT(&huart0, completed, UART_BLOCK_BYTES));
        }
        /* Keep servicing Process: the architecture counter does not wake WFI,
         * and TEMT has no dedicated transfer-complete interrupt. */
    }
}
