/* Linux host-only register model. W1C/alias semantics and electrical behavior
   still require silicon; these tests check API boundaries and issued writes. */
#include "r328_hal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

static uint32_t fake_tick;
uint32_t HAL_GetTick(void)
{
    return fake_tick++;
}

/* UART output is captured per explicit handle; optional FIFO stalls advance
 * the fake tick and exercise one deadline across formatting fragments. */
static UART_HandleTypeDef *printed_uart;
static uint8_t printed[256];
static uint32_t printed_size, fifo_stalls, fifo_reads;
uint32_t R328_HAL_TestRead32(volatile uint32_t *reg)
{
    if (printed_uart != NULL && reg == &printed_uart->Instance->USR && fifo_stalls != 0U)
    {
        if (fifo_reads++ < fifo_stalls)
        {
            return 0U;
        }
        fifo_reads = 0U;
    }
    return *reg;
}
void R328_HAL_TestWrite32(volatile uint32_t *reg, uint32_t value)
{
    if (printed_uart != NULL && reg == &printed_uart->Instance->DATA)
    {
        assert(printed_uart->Lock == HAL_LOCKED && printed_uart->State == HAL_UART_STATE_BUSY_TX);
        assert(printed_size < sizeof(printed));
        printed[printed_size++] = (uint8_t)value;
    }
    *reg = value;
}
static void expect_print(const char *expected)
{
    assert(printed_size == strlen(expected));
    assert(memcmp(printed, expected, printed_size) == 0);
    assert(printed_uart->Lock == HAL_UNLOCKED && printed_uart->State == HAL_UART_STATE_READY);
    printed_size = 0U;
}
static void test_uart_print(void)
{
    UART_HandleTypeDef uart = {.Instance = UART1, .State = HAL_UART_STATE_READY};
    printed_uart = &uart;
    UART1->USR = 2U;
    UART1->LSR = 1U << 6;
    UART0->DATA = 0xA5U;
    assert(HAL_UART_PrintString(&uart, "hello\r\n", 10U) == HAL_OK);
    expect_print("hello\r\n");
    assert(UART0->DATA == 0xA5U); /* Output must not implicitly use UART0. */
    assert(HAL_UART_PrintString(&uart, "", 0U) == HAL_OK);
    expect_print("");
    const uint32_t values[] = {0U, 9U, 10U, UINT32_MAX};
    const char *const expected[] = {"0", "9", "10", "4294967295"};
    for (uint32_t i = 0U; i < 4U; ++i)
    {
        assert(HAL_UART_PrintU32(&uart, values[i], 10U) == HAL_OK);
        expect_print(expected[i]);
    }
    assert(HAL_UART_PrintHex(&uart, 0x123ABU, 10U) == HAL_OK);
    expect_print("000123AB");
    assert(HAL_UART_PrintHex(&uart, UINT32_MAX, 10U) == HAL_OK);
    expect_print("FFFFFFFF");
    assert(HAL_UART_PrintValue(&uart, "tick=", UINT32_MAX, 10U) == HAL_OK);
    expect_print("tick=4294967295\r\n");
    uint8_t bytes[23];
    for (uint32_t i = 0U; i < sizeof(bytes); ++i)
    {
        bytes[i] = (uint8_t)i;
    }
    assert(HAL_UART_HexDump(&uart, bytes, sizeof(bytes), 10U) == HAL_OK);
    expect_print("00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 11 12 13 14 15 16 \r\n");
    assert(HAL_UART_HexDump(&uart, NULL, 0U, 10U) == HAL_OK);
    expect_print("\r\n");
    assert(HAL_UART_PrintString(&uart, NULL, 10U) == HAL_ERROR);
    assert(HAL_UART_PrintValue(&uart, NULL, 1U, 10U) == HAL_ERROR);
    assert(HAL_UART_HexDump(&uart, NULL, 1U, 10U) == HAL_ERROR);
    assert(HAL_UART_PrintHex(NULL, 1U, 10U) == HAL_ERROR);
    uart.State = HAL_UART_STATE_RESET;
    assert(HAL_UART_PrintU32(&uart, 1U, 10U) == HAL_ERROR);
    uart.State = HAL_UART_STATE_READY;
    uart.Lock = HAL_LOCKED;
    assert(HAL_UART_PrintValue(&uart, "a", 1U, 10U) == HAL_BUSY);
    assert(HAL_UART_HexDump(&uart, bytes, sizeof(bytes), 10U) == HAL_BUSY);
    assert(printed_size == 0U);
    uart.Lock = HAL_UNLOCKED;
    fifo_stalls = 2U;
    fake_tick = UINT32_MAX - 2U;
    assert(HAL_UART_PrintValue(&uart, "a", 1U, 5U) == HAL_TIMEOUT);
    expect_print("a1"); /* CRLF must not get a fresh timeout. */
    fifo_reads = 0U;
    assert(HAL_UART_HexDump(&uart, bytes, sizeof(bytes), 5U) == HAL_TIMEOUT);
    expect_print("00");
    fifo_reads = 0U;
    fake_tick = 0U;
    assert(HAL_UART_HexDump(&uart, bytes, sizeof(bytes), 100U) == HAL_TIMEOUT);
    /* The first 16-byte chunk consumes most of the deadline; the second
     * chunk must not restart it. */
    expect_print("00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 1");
    fifo_stalls = 0U;
    UART1->LSR = 0U; /* Final shifter drain is part of the same timeout. */
    assert(HAL_UART_PrintHex(&uart, 0U, 2U) == HAL_TIMEOUT);
    expect_print("00000000");
    printed_uart = NULL;
}

static void map_region(uintptr_t address, size_t size)
{
    void *result = mmap((void *)address, size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (result == MAP_FAILED)
    {
        perror("mmap peripheral test region");
    }
    assert(result == (void *)address);
}
static void test_gpio(void)
{
    /* Regression: PC16 together with PC0 must preserve other CFG2 bits. */
    GPIOC->CFG[2] = 0x88888888U;
    GPIO_InitTypeDef init = {.Pin = GPIO_PIN_0 | GPIO_PIN_16,
                             .Mode = GPIO_MODE_OUTPUT_PP,
                             .Pull = GPIO_PULLUP,
                             .Drive = GPIO_DRIVE_LEVEL3};
    assert(HAL_GPIO_Init(GPIOC, &init) == HAL_OK);
    assert(GPIOC->CFG[0] == 1U && GPIOC->CFG[2] == 0x88888889U);
    assert(GPIOC->PUL[0] == 1U && GPIOC->PUL[1] == 1U);
    assert(GPIOC->DRV[0] == 3U && GPIOC->DRV[1] == 3U);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0 | GPIO_PIN_16, GPIO_PIN_SET);
    assert(GPIOC->DAT == 0x10001U);
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_16);
    assert(GPIOC->DAT == 1U);
    assert(HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_0) == GPIO_PIN_SET);
    init.Pin = GPIO_PIN_5; /* PC5 is not bonded on this package. */
    assert(HAL_GPIO_Init(GPIOC, &init) == HAL_ERROR);
    assert(HAL_GPIO_Init(NULL, &init) == HAL_ERROR);
    init.Pin = GPIO_PIN_8 | GPIO_PIN_9;
    init.Mode = GPIO_MODE_AF_PP;
    init.Alternate = 5U;
    assert(HAL_GPIO_Init(GPIOB, &init) == HAL_OK);
    assert(GPIOB->CFG[1] == 0x55U);
}
static void test_uart(void)
{
    RCC_APB2_CFG = UINT32_MAX;
    assert(HAL_RCC_APB2_Config24M() == HAL_OK);
    assert(HAL_RCC_GetPCLK2Freq() == 24000000U);
    UART_HandleTypeDef uart = {
        .Instance = UART0,
        .Init = {115200U, UART_WORDLENGTH_8B, UART_STOPBITS_1, UART_PARITY_NONE}};
    assert(HAL_UART_Init(&uart) == HAL_OK);
    assert(UART0->DATA == 13U && UART0->IER_DLH == 0U);
    assert(UART0->LCR == 3U && UART0->IIR_FCR == 7U);
    assert((RCC_UART_BGR & 0x10001U) == 0x10001U);
    uint8_t byte = 0xA5U;
    UART0->USR = 2U;
    UART0->LSR = 0x40U;
    assert(HAL_UART_Transmit(&uart, &byte, 1U, 10U) == HAL_OK);
    assert(UART0->DATA == 0xA5U && uart.State == HAL_UART_STATE_READY);
    UART0->USR = 0U;
    fake_tick = UINT32_MAX - 2U; /* Timeout remains correct across tick wrap. */
    assert(HAL_UART_Transmit(&uart, &byte, 1U, 10U) == HAL_TIMEOUT);
    assert(uart.Lock == HAL_UNLOCKED && uart.State == HAL_UART_STATE_READY);
    UART0->LSR = 0U;
    assert(HAL_UART_Receive(&uart, &byte, 1U, 0U) == HAL_TIMEOUT);
    UART0->LSR = 1U;
    UART0->DATA = 0U;
    byte = 0xFFU;
    assert(HAL_UART_Receive(&uart, &byte, 1U, 0U) == HAL_OK && byte == 0U);
    UART0->LSR = 5U;
    assert(HAL_UART_Receive(&uart, &byte, 1U, 0U) == HAL_ERROR);
    assert(uart.ErrorCode == HAL_UART_ERROR_PE && uart.Lock == HAL_UNLOCKED);
    uart.Lock = HAL_LOCKED;
    assert(HAL_UART_Transmit(&uart, &byte, 1U, 0U) == HAL_BUSY);
    uart.Lock = HAL_UNLOCKED;
    uart.Init.BaudRate = UINT32_MAX;
    assert(HAL_UART_Init(&uart) == HAL_ERROR);
    uart.Init.BaudRate = 115200U;
    RCC_APB2_CFG = 3U; /* /4 APB2: 6 MHz, rounded divisor 3 exceeds 3% error. */
    assert(HAL_UART_Init(&uart) == HAL_ERROR);
    uart.Init.BaudRate = 9600U;
    assert(HAL_UART_Init(&uart) == HAL_OK && UART0->DATA == 39U);
    RCC_APB2_CFG = 1UL << 24; /* Unsupported source must not silently use CPU clock. */
    assert(HAL_UART_Init(&uart) == HAL_ERROR);
    assert(HAL_UART_DeInit(&uart) == HAL_OK);
}
static uint32_t irq_calls;
static void irq_handler(void *context)
{
    ++*(uint32_t *)context;
}
static void test_gic(void)
{
    R328_REG32(R328_GICD_BASE + 4U) = 5U; /* 192 implemented IDs */
    assert(HAL_GIC_Init() == HAL_OK);
    assert(HAL_GIC_RegisterIRQ(UART0_IRQn, irq_handler, &irq_calls, 0x80U) == HAL_OK);
    assert(HAL_GIC_RegisterIRQ(UART0_IRQn, irq_handler, NULL, 0x80U) == HAL_BUSY);
    assert(HAL_GIC_EnableIRQ(UART0_IRQn) == HAL_OK);
    R328_REG32(R328_GICC_BASE + 0xCU) = 108U;
    HAL_GIC_IRQHandler();
    assert(irq_calls == 1U && R328_REG32(R328_GICC_BASE + 0x10U) == 108U);
    R328_REG32(R328_GICC_BASE + 0xCU) = 1023U;
    R328_REG32(R328_GICC_BASE + 0x10U) = 0xDEADU;
    HAL_GIC_IRQHandler();
    assert(irq_calls == 1U && R328_REG32(R328_GICC_BASE + 0x10U) == 0xDEADU);
    assert(HAL_GIC_UnregisterIRQ(UART0_IRQn, NULL) == HAL_BUSY);
    assert(HAL_GIC_UnregisterIRQ(UART0_IRQn, &irq_calls) == HAL_OK);
    assert(HAL_GIC_EnableIRQ(UART0_IRQn) == HAL_ERROR);
    assert(HAL_GIC_RegisterIRQ((IRQn_Type)-1, irq_handler, NULL, 0U) == HAL_ERROR);
}
static uint32_t timer_calls;
static void timer_callback(TIM_HandleTypeDef *handle)
{
    UNUSED(handle);
    ++timer_calls;
}
static void test_timer(void)
{
    TIM_HandleTypeDef timer = {.Instance = TIM1,
                               .Init = {TIM_PRESCALER_128, 187500U, TIM_MODE_CONTINUOUS},
                               .PeriodElapsedCallback = timer_callback};
    assert(HAL_TIM_Base_Init(&timer) == HAL_OK);
    assert(TIM1->INTV == 187500U && TIM1->CTRL == 0x74U);
    TIM_HandleTypeDef duplicate = {.Instance = TIM1, .Init = timer.Init};
    assert(HAL_TIM_Base_Init(&duplicate) == HAL_BUSY);
    /* A stuck reload bit must return without depending on a functioning tick. */
    assert(HAL_TIM_Base_Start_IT(&timer) == HAL_TIMEOUT);
    assert(timer.State == HAL_TIM_STATE_READY && timer.Lock == HAL_UNLOCKED);
    timer.State = HAL_TIM_STATE_BUSY;
    TIMER->IRQ_STA = 3U; /* Both pending: clearing TIM1 must not clear TIM0. */
    HAL_TIM_IRQHandler(&timer);
    assert(TIMER->IRQ_STA == 2U && timer_calls == 1U);
    assert(HAL_TIM_Base_Stop_IT(&timer) == HAL_OK);
    assert(HAL_TIM_Base_DeInit(&timer) == HAL_OK);
    assert(HAL_TIM_Base_Init(&duplicate) == HAL_OK);
    assert(HAL_TIM_Base_DeInit(&duplicate) == HAL_OK);
}
int main(void)
{
    map_region(0x03000000U, 0x30000U);
    map_region(0x05000000U, 0x1000U);
    test_gpio();
    test_uart();
    test_uart_print();
    test_gic();
    test_timer();
    puts("GPIO / UART / GIC / TIM register logic passed (host model, not silicon)");
    return 0;
}
