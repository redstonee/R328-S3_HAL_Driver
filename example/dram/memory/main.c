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

static void uart_poll(void)
{
    uint8_t byte;
    if (HAL_UART_GetRxFifoLevel(&huart0) != 0U && HAL_UART_Receive(&huart0, &byte, 1U) == HAL_OK)
    {
        (void)HAL_UART_Transmit(&huart0, &byte, 1U);
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

#define DRAM_BASE 0x40000000U
#define DRAM_END 0x48000000U

extern uint32_t __image_start__;
extern uint32_t __static_end__;
extern uint32_t __stack_bottom__;
extern uint32_t __stack_top__;

static void PrintHex(const char *label, uint32_t value)
{
    (void)HAL_UART_PrintString(&huart0, label);
    (void)HAL_UART_PrintString(&huart0, "0x");
    HAL_UART_PrintHex(&huart0, value);
    (void)HAL_UART_PrintString(&huart0, "\r\n");
}

static void Fail(uintptr_t address, uint32_t expected, uint32_t actual)
{
    PrintHex("DRAM FAIL address=", (uint32_t)address);
    PrintHex("expected=", expected);
    PrintHex("actual=", actual);
    idle();
}

static void TestDataBus(uintptr_t address)
{
    volatile uint32_t *word = (volatile uint32_t *)address;
    /* Walk both a single 1 and a single 0 through all 32 data bits. */
    for (uint32_t bit = 1U; bit != 0U; bit <<= 1U)
    {
        for (uint32_t invert = 0U; invert < 2U; ++invert)
        {
            uint32_t expected = invert == 0U ? bit : ~bit;
            *word = expected;
            R328_DSB();
            uint32_t actual = *word;
            if (actual != expected)
            {
                Fail(address, expected, actual);
            }
        }
    }
    (void)HAL_UART_PrintString(&huart0, "data bus PASS (walking 1/0)\r\n");
}

static void TestRange(uintptr_t begin, uintptr_t end, uint32_t pattern, uint32_t use_address)
{
    /* Write the whole region before verifying it: testing small chunks
     * independently would miss aliases between different chunks. */
    for (uintptr_t address = begin; address < end; address += sizeof(uint32_t))
    {
        uint32_t expected = pattern ^ (use_address != 0U ? (uint32_t)address : 0U);
        *(volatile uint32_t *)address = expected;
    }
    R328_DSB();
    for (uintptr_t address = begin; address < end; address += sizeof(uint32_t))
    {
        uint32_t expected = pattern ^ (use_address != 0U ? (uint32_t)address : 0U);
        uint32_t actual = *(volatile uint32_t *)address;
        if (actual != expected)
        {
            Fail(address, expected, actual);
        }
    }
    R328_DSB();
}

static void TestMemory(void)
{
    uintptr_t begin = ((uintptr_t)&__static_end__ + 63U) & ~(uintptr_t)63U;
    uintptr_t end = (uintptr_t)&__stack_bottom__ & ~(uintptr_t)3U;
    uintptr_t origin = (uintptr_t)&__image_start__;
    uintptr_t top = (uintptr_t)&__stack_top__;
    uint32_t control;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(control));
    if (origin < DRAM_BASE || origin >= DRAM_END || begin < origin || end > top || top > DRAM_END ||
        begin >= end)
    {
        (void)HAL_UART_PrintString(&huart0, "DRAM SKIP: use a DDR build with initialized DRAM\r\n");
        idle();
    }
    if ((control & 5U) != 0U)
    {
        (void)HAL_UART_PrintString(&huart0, "DRAM SKIP: MMU and D-cache must be disabled\r\n");
        idle();
    }

    PrintHex("begin=", (uint32_t)begin);
    PrintHex("end (exclusive)=", (uint32_t)end);
    HAL_UART_PrintValue(&huart0, "tested bytes=", (uint32_t)(end - begin));
    uint32_t start = HAL_GetTick();
    TestDataBus(begin);
    static const uint32_t patterns[] = {0U, 0xFFFFFFFFU, 0x55555555U, 0xAAAAAAAAU, 0U, 0xFFFFFFFFU};
    static const char *const names[] = {"zero", "ones", "55", "AA", "address", "~address"};
    for (uint32_t pass = 0U; pass < sizeof(patterns) / sizeof(patterns[0]); ++pass)
    {
        (void)HAL_UART_PrintString(&huart0, "testing ");
        (void)HAL_UART_PrintString(&huart0, names[pass]);
        (void)HAL_UART_PrintString(&huart0, "...\r\n");
        uint32_t pass_start = HAL_GetTick();
        TestRange(begin, end, patterns[pass], pass >= 4U);
        HAL_UART_PrintValue(&huart0, "PASS ms=", (uint32_t)(HAL_GetTick() - pass_start));
    }
    (void)HAL_UART_PrintString(&huart0, "DRAM PASS (free region, 6 patterns + data bus)\r\n");
    HAL_UART_PrintValue(&huart0, "total ms=", (uint32_t)(HAL_GetTick() - start));
}

int main(void)
{
    app_init("DRAM free-memory test");
    TestMemory();
    for (;;)
    {
        HAL_Delay(1000U);
        HAL_UART_PrintValue(&huart0, "tick=", HAL_GetTick());
    }
}
