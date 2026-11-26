#include "main.h"
#include "gpio_example.h"

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

int main(void)
{
    app_init("GPIO / write, read back and toggle");
    static const GPIO_InitTypeDef output = {.Pin = EXAMPLE_GPIO_OUTPUT_PIN,
                                            .Mode = GPIO_MODE_OUTPUT_PP,
                                            .Pull = GPIO_NOPULL,
                                            .Drive = GPIO_DRIVE_LEVEL1};
    /* Preload the level before selecting the output function. */
    HAL_GPIO_WritePin(EXAMPLE_GPIO_OUTPUT_PORT, EXAMPLE_GPIO_OUTPUT_PIN, GPIO_PIN_RESET);
    require_ok("GPIO init=", HAL_GPIO_Init(EXAMPLE_GPIO_OUTPUT_PORT, &output));
    for (;;)
    {
        HAL_GPIO_WritePin(EXAMPLE_GPIO_OUTPUT_PORT, EXAMPLE_GPIO_OUTPUT_PIN, GPIO_PIN_SET);
        HAL_UART_PrintValue(&huart0, "write high, read=",
                            HAL_GPIO_ReadPin(EXAMPLE_GPIO_OUTPUT_PORT, EXAMPLE_GPIO_OUTPUT_PIN));
        HAL_Delay(500U);
        HAL_GPIO_WritePin(EXAMPLE_GPIO_OUTPUT_PORT, EXAMPLE_GPIO_OUTPUT_PIN, GPIO_PIN_RESET);
        HAL_UART_PrintValue(&huart0, "write low, read=",
                            HAL_GPIO_ReadPin(EXAMPLE_GPIO_OUTPUT_PORT, EXAMPLE_GPIO_OUTPUT_PIN));
        HAL_Delay(500U);
        HAL_GPIO_TogglePin(EXAMPLE_GPIO_OUTPUT_PORT, EXAMPLE_GPIO_OUTPUT_PIN);
        HAL_UART_PrintValue(&huart0, "toggle, read=",
                            HAL_GPIO_ReadPin(EXAMPLE_GPIO_OUTPUT_PORT, EXAMPLE_GPIO_OUTPUT_PIN));
        HAL_Delay(500U);
    }
}
