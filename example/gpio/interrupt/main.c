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

static void uart_poll(void)
{
    uint8_t byte;
    if (HAL_UART_Receive(&huart0, &byte, 1U, 0U) == HAL_OK)
    {
        (void)HAL_UART_Transmit(&huart0, &byte, 1U, UART_TIMEOUT_MS);
    }
}

static void idle(void) __attribute__((noreturn));
static void idle(void)
{
    /* Polling RX needs the CPU; the architecture counter does not wake WFI. */
    for (;;)
    {
        uart_poll();
    }
}

static volatile uint32_t edges;
static volatile uint32_t last_edge;

void HAL_GPIO_EXTI_Callback(GPIO_TypeDef *port, uint32_t pin)
{
    if (port == EXAMPLE_GPIO_INPUT_PORT && pin == EXAMPLE_GPIO_INPUT_PIN)
    {
        ++edges;
        last_edge = HAL_GetTick();
    }
}

int main(void)
{
    app_init("GPIO / both-edge EINT with foreground debounce");
    static const GPIO_EXTI_InitTypeDef input = {
        .Pin = EXAMPLE_GPIO_INPUT_PIN, .Trigger = GPIO_EXTI_BOTH, .Pull = GPIO_PULLUP};
    require_ok("EXTI init=", HAL_GPIO_EXTI_Init(EXAMPLE_GPIO_INPUT_PORT, &input));
    (void)HAL_UART_PrintString(&huart0, "Change the input level; press q to release EINT.\r\n",
                               UART_TIMEOUT_MS);
    uint32_t reported = 0U;
    for (;;)
    {
        /* Snapshot both IRQ-owned values in one critical section. */
        uint32_t state = R328_IRQ_Save();
        uint32_t count = edges;
        uint32_t changed = last_edge;
        R328_IRQ_Restore(state);
        if (count != reported && (uint32_t)(HAL_GetTick() - changed) >= EXAMPLE_GPIO_DEBOUNCE_MS)
        {
            reported = count;
            HAL_UART_PrintValue(&huart0, "raw edges=", count, UART_TIMEOUT_MS);
            HAL_UART_PrintValue(&huart0, "settled level=",
                                HAL_GPIO_ReadPin(EXAMPLE_GPIO_INPUT_PORT, EXAMPLE_GPIO_INPUT_PIN),
                                UART_TIMEOUT_MS);
        }
        uint8_t byte;
        if (HAL_UART_Receive(&huart0, &byte, 1U, 0U) == HAL_OK && byte == 'q')
        {
            require_ok("EXTI deinit=",
                       HAL_GPIO_EXTI_DeInit(EXAMPLE_GPIO_INPUT_PORT, EXAMPLE_GPIO_INPUT_PIN));
            (void)HAL_UART_PrintString(&huart0, "EINT released; input mux disabled.\r\n",
                                       UART_TIMEOUT_MS);
            idle();
        }
    }
}
