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

#define LINE_CAPACITY 64U
#define LINE_IDLE_TIMEOUT_MS 5000U

int main(void)
{
    app_init("UART0 / bounded line input");
    (void)HAL_UART_PrintString(
        &huart0, "Enter a line; CR/LF completes, Backspace edits, idle timeout is 5 s.\r\n");
    uint8_t line[LINE_CAPACITY];
    uint32_t used = 0U, overflow = 0U, skip_lf = 0U, last_byte = HAL_GetTick();
    for (;;)
    {
        uint8_t byte;
        HAL_StatusTypeDef status = HAL_UART_GetRxFifoLevel(&huart0) != 0U
                                       ? HAL_UART_Receive(&huart0, &byte, 1U)
                                       : HAL_TIMEOUT;
        if (status == HAL_OK)
        {
            last_byte = HAL_GetTick();
            if (byte == '\n' && skip_lf != 0U)
            {
                skip_lf = 0U;
                continue;
            }
            skip_lf = byte == '\r';
            if (byte == '\r' || byte == '\n')
            {
                (void)HAL_UART_PrintString(&huart0, "\r\n");
                if (overflow != 0U)
                {
                    (void)HAL_UART_PrintString(&huart0,
                                               "Line discarded: capacity is 63 bytes.\r\n");
                }
                else
                {
                    HAL_UART_PrintValue(&huart0, "line bytes=", used);
                    require_ok("UART TX=", HAL_UART_Transmit(&huart0, line, used));
                    (void)HAL_UART_PrintString(&huart0, "\r\n");
                }
                used = 0U;
                overflow = 0U;
            }
            else if ((byte == 8U || byte == 127U) && overflow == 0U && used != 0U)
            {
                --used;
                (void)HAL_UART_PrintString(&huart0, "\b \b");
            }
            else if (byte >= 32U && byte <= 126U && overflow == 0U)
            {
                if (used < sizeof(line) - 1U)
                {
                    line[used++] = byte;
                    require_ok("UART TX=", HAL_UART_Transmit(&huart0, &byte, 1U));
                }
                else
                {
                    overflow = 1U; /* Consume through the terminator; never execute a prefix. */
                }
            }
        }
        else if (status != HAL_TIMEOUT && status != HAL_BUSY)
        {
            HAL_UART_PrintValue(&huart0, "UART RX=", status);
            HAL_UART_PrintValue(&huart0, "UART ErrorCode=", huart0.ErrorCode);
            used = 0U;
            overflow = 0U;
        }
        if ((used != 0U || overflow != 0U) &&
            (uint32_t)(HAL_GetTick() - last_byte) >= LINE_IDLE_TIMEOUT_MS)
        {
            used = 0U;
            overflow = 0U;
            (void)HAL_UART_PrintString(&huart0, "\r\nPartial line discarded: idle timeout.\r\n");
        }
    }
}
