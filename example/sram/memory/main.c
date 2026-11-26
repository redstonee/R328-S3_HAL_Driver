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

extern uint32_t __static_end__;
extern uint32_t __stack_bottom__;
extern uint32_t __stack_top__;
extern uint32_t __image_start__;

static void PrintAddress(const char *label, uintptr_t address)
{
    (void)HAL_UART_PrintString(&huart0, label, UART_TIMEOUT_MS);
    (void)HAL_UART_PrintString(&huart0, "0x", UART_TIMEOUT_MS);
    HAL_UART_PrintHex(&huart0, (uint32_t)address, UART_TIMEOUT_MS);
    (void)HAL_UART_PrintString(&huart0, "\r\n", UART_TIMEOUT_MS);
}

static void TestMemory(void)
{
    uintptr_t begin = (uintptr_t)&__static_end__;
    uintptr_t end = (uintptr_t)&__stack_bottom__;
    uint32_t start = HAL_GetTick();
    PrintAddress("free begin=", begin);
    PrintAddress("free end (exclusive)=", end);
    HAL_UART_PrintValue(&huart0, "tested bytes=", (uint32_t)(end - begin), UART_TIMEOUT_MS);
    /* Fill the entire free region before reading, to expose address aliasing.
     * Code, statics and every exception stack remain outside the test region.
     */
    for (uint32_t pass = 0U; pass < 2U; ++pass)
    {
        uint32_t pattern = pass == 0U ? 0x55AA33CCU : 0xAA55CC33U;
        for (uintptr_t p = begin; p < end; p += sizeof(uint32_t))
        {
            *(volatile uint32_t *)p = (uint32_t)p ^ pattern;
        }
        __asm__ volatile("dsb sy" ::: "memory");
        for (uintptr_t p = begin; p < end; p += sizeof(uint32_t))
        {
            uint32_t expected = (uint32_t)p ^ pattern;
            uint32_t actual = *(volatile uint32_t *)p;
            if (actual != expected)
            {
                PrintAddress("SRAM FAIL address=", p);
                PrintAddress("expected=", expected);
                PrintAddress("actual=", actual);
                idle();
            }
        }
    }
    (void)HAL_UART_PrintString(&huart0, "SRAM PASS (address pattern + inverse)\r\n",
                               UART_TIMEOUT_MS);
    HAL_UART_PrintValue(&huart0, "test ms=", (uint32_t)(HAL_GetTick() - start), UART_TIMEOUT_MS);
}

int main(void)
{
    app_init("SRAM free-memory test");
    PrintAddress("runtime origin=", (uintptr_t)&__image_start__);
    PrintAddress("SRAM top=", (uintptr_t)&__stack_top__);
    TestMemory();
    for (;;)
    {
        HAL_Delay(1000U);
        HAL_UART_PrintValue(&huart0, "tick=", HAL_GetTick(), UART_TIMEOUT_MS);
    }
}
