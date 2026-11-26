#include "main.h"

#define UART_TIMEOUT_MS 100U
#define UART_BLOCK_BYTES 8U
static UART_HandleTypeDef huart0;
static uint8_t rx_blocks[2][UART_BLOCK_BYTES];
static volatile uint32_t rx_ready, tx_ready, uart_error;
static uint32_t receive_index;
static volatile uint32_t error_rx_remaining, error_rx_pin;
static volatile uint32_t error_lcr, error_mcr, error_halt;
static volatile uint32_t error_lsr, error_fifo_level;

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

/* Normal callbacks run in UART/DMA IRQ context, after unlocking.
 * Only record flags/snapshots here; print and process data in the main loop. */
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
    if (handle == &huart0 && uart_error == HAL_UART_ERROR_NONE)
    {
        /* The driver has already saved the clear-on-read LSR error bits. Do not
         * read IIR/LSR here: that would acknowledge or clear another event. */
        error_lsr = handle->RxErrorLineStatus;
        error_fifo_level = handle->RxErrorFifoLevel;
        error_rx_remaining = handle->Rx.Count;
        error_rx_pin = (uint32_t)HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_1);
        error_lcr = handle->Instance->LCR;
        error_mcr = handle->Instance->MCR;
        error_halt = handle->Instance->HALT;
        uart_error |= handle->ErrorCode;
    }
}

static void report_uart_error(uint32_t error)
{
    (void)HAL_UART_PrintValue(&huart0, "UART ErrorCode=", error);
    if ((error & HAL_UART_ERROR_FE) != 0U)
    {
        (void)HAL_UART_PrintString(&huart0, "FE: receiver did not detect a valid stop bit.\r\n");
    }
    if ((error & HAL_UART_ERROR_BI) != 0U)
    {
        (void)HAL_UART_PrintString(&huart0, "BI: receiver detected a BREAK (RX held low).\r\n");
    }
    (void)HAL_UART_PrintString(&huart0, "First error LSR=0x");
    (void)HAL_UART_PrintHex(&huart0, error_lsr);
    (void)HAL_UART_PrintString(&huart0, "\r\n");
    (void)HAL_UART_PrintValue(&huart0, "FIFO level at first error=", error_fifo_level);
    (void)HAL_UART_PrintValue(&huart0, "RX bytes remaining=", error_rx_remaining);
    (void)HAL_UART_PrintValue(&huart0, "PH1 level at error callback=", error_rx_pin);
    (void)HAL_UART_PrintValue(&huart0, "UART PCLK Hz=", HAL_RCC_GetPCLK2Freq());
    (void)HAL_UART_PrintString(&huart0, "LCR=0x");
    (void)HAL_UART_PrintHex(&huart0, error_lcr);
    (void)HAL_UART_PrintString(&huart0, " MCR=0x");
    (void)HAL_UART_PrintHex(&huart0, error_mcr);
    (void)HAL_UART_PrintString(&huart0, " HALT=0x");
    (void)HAL_UART_PrintHex(&huart0, error_halt);
    (void)HAL_UART_PrintString(&huart0, "\r\n");
}

int main(void)
{
    app_init("UART0 IT full-duplex fixed-size echo / 115200 8N1");
    (void)HAL_UART_PrintString(
        &huart0, "Send exactly 8 bytes (e.g. 12345678), then wait for the raw echo.\r\n"
                 "No newline/idle detection; disable terminal local echo.\r\n");
    require_ok("RX start=", HAL_UART_Receive_IT(&huart0, rx_blocks[0], UART_BLOCK_BYTES));
    static const uint8_t ready[] = "\r\nReady.\r\n";
    require_ok("TX start=", HAL_UART_Transmit_IT(&huart0, ready, sizeof(ready) - 1U));
    for (;;)
    {
        if (uart_error != HAL_UART_ERROR_NONE)
        {
            uint32_t saved_error = uart_error;
            /* The error callback preserves the first failure snapshot even if
             * aborting the other direction produces a second callback. */
            if (HAL_UART_Abort(&huart0) != HAL_OK)
            {
                Error_Handler();
            }
            report_uart_error(saved_error);
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
        /* No Process call is needed. Leave this loop running to avoid a
         * check-then-WFI race with the completion flags. */
    }
}
