/* Real LEDC driver with a bounded FIFO, W1C flags and self-clearing reset model. */
#include "board.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

static uint32_t tick, queued, consumed, target, logged, reset_reads, latch_requests;
static uint32_t colors[LEDC_MAX_LEDS], log_words[LEDC_MAX_LEDS];
static uint32_t injected_error, prefilled;
static int stall, stuck_reset, stuck_latch, invalid_depth;

uint32_t HAL_GetTick(void)
{
    return tick++;
}
void Error_Handler(void)
{
    abort();
}

uint32_t R328_HAL_TestRead32(volatile uint32_t *reg)
{
    if (reg == &LEDC->CTRL && (*reg & (1U << 10)) != 0U && !stuck_latch)
    {
        if (--reset_reads == 0U)
        {
            *reg &= ~(1U << 10);
        }
    }
    if (reg == &LEDC->INT_STS)
    {
        if ((LEDC->CTRL & 1U) != 0U)
        {
            if (!stall)
            {
                uint32_t words = queued < 7U ? queued : 7U;
                queued -= words;
                consumed += words;
                if (consumed == target)
                {
                    LEDC->CTRL &= ~1U;
                    *reg |= 1U;
                }
            }
            *reg |= injected_error;
        }
        return (*reg & 0x1BU) | ((invalid_depth ? 33U : queued) << 10);
    }
    return *reg;
}

void R328_HAL_TestWrite32(volatile uint32_t *reg, uint32_t value)
{
    if (reg == &RCC_LEDC_BGR && (value & (1U << 16)) == 0U)
    {
        memset(LEDC, 0, sizeof(*LEDC));
        queued = consumed = 0U;
    }
    if (reg == &LEDC->INT_STS)
    {
        assert((value & ~0x1BU) == 0U); /* Never echo read-only FIFO status to W1C. */
        *reg &= ~value;
        return;
    }
    if (reg == &LEDC->CTRL)
    {
        if ((value & 2U) != 0U)
        {
            queued = consumed = target = logged = 0U;
            LEDC->INT_STS = 0U;
            if (!stuck_reset)
            {
                value &= ~3U;
            }
        }
        if ((value & (1U << 10)) != 0U)
        {
            assert((LEDC->CTRL & 1U) == 0U);
            ++latch_requests;
            reset_reads = 3U;
        }
        if ((value & 1U) != 0U)
        {
            assert(LEDC->INT_CTRL == 0U && LEDC->DMA_CTRL == 0U);
            target = (value >> 16) & 0x1FFFU;
            prefilled = queued;
            assert(queued == (target < 32U ? target : 32U));
        }
    }
    if (reg == &LEDC->DATA)
    {
        assert(queued < 32U && logged < LEDC_MAX_LEDS);
        assert((value & 0xFF000000U) == 0U);
        ++queued;
        log_words[logged++] = value;
    }
    *reg = value;
}

static void reset_model(void)
{
    memset(LEDC, 0, sizeof(*LEDC));
    memset(GPIOE, 0, sizeof(*GPIOE));
    memset(&hledc, 0, sizeof(hledc));
    RCC_LEDC_CLK = RCC_LEDC_BGR = 0U;
    tick = queued = consumed = target = logged = reset_reads = latch_requests = 0U;
    injected_error = prefilled = 0U;
    stall = stuck_reset = stuck_latch = invalid_depth = 0;
    for (uint32_t i = 0U; i < LEDC_MAX_LEDS; ++i)
    {
        colors[i] = 0xA5000000U | HAL_LEDC_ColorRGB((uint8_t)i, (uint8_t)(i >> 2), (uint8_t)~i);
    }
}

static void test_init_and_pins(void)
{
    reset_model();
    RCC_LEDC_BGR = 1U << 7; /* Preserve unrelated bits through RMW. */
    assert(Board_LEDC_Init(4U) == HAL_OK);
    assert(RCC_LEDC_CLK == 0x80000000U);
    assert(RCC_LEDC_BGR == ((1U << 7) | (1U << 16) | 1U));
    assert(((GPIOE->CFG[0] >> 8) & 7U) == 2U);
    assert(LEDC->CTRL == 0x3CU);
    assert(LEDC->TIMING == ((22U << 21) | (6U << 16) | (6U << 6) | 22U));
    assert(LEDC->RESET_TIMING == ((7199U << 16) | 3U));
    assert(LEDC->FINISH == (1199U << 16));
    assert(LEDC->WAIT_TIME0 == 0U && LEDC->WAIT_TIME1 == 0U);
    assert(LEDC->INT_CTRL == 0U && LEDC->DMA_CTRL == 0U);
    assert(latch_requests == 1U && hledc.State == HAL_LEDC_STATE_READY);
    assert(HAL_LEDC_ColorRGB(0x12U, 0x34U, 0x56U) == 0x341256U);
}

static void test_frames_and_latch(void)
{
    const uint32_t counts[] = {1U, 4U, 32U, 33U, 131U, LEDC_MAX_LEDS};
    for (uint32_t i = 0U; i < sizeof(counts) / sizeof(counts[0]); ++i)
    {
        reset_model();
        uint32_t count = counts[i];
        assert(Board_LEDC_Init(count) == HAL_OK);
        for (uint32_t repeat = 0U; repeat < 2U; ++repeat)
        {
            uint32_t before = latch_requests;
            assert(HAL_LEDC_Transmit(&hledc, colors, count, 1000U) == HAL_OK);
            assert(logged == count && consumed == count && queued == 0U);
            assert(prefilled == (count < 32U ? count : 32U));
            assert(latch_requests == before + 1U && reset_reads == 0U);
            for (uint32_t word = 0U; word < count; ++word)
            {
                assert(log_words[word] == (colors[word] & 0xFFFFFFU));
            }
            assert(hledc.State == HAL_LEDC_STATE_READY && hledc.Lock == HAL_UNLOCKED);
            assert(hledc.ErrorCode == HAL_LEDC_ERROR_NONE);
        }
    }
}

static void test_validation_and_busy(void)
{
    reset_model();
    assert(HAL_LEDC_Init(NULL) == HAL_ERROR);
    assert(HAL_LEDC_DeInit(NULL) == HAL_ERROR);
    assert(HAL_LEDC_Transmit(NULL, colors, 4U, 100U) == HAL_ERROR);
    assert(Board_LEDC_Init(0U) == HAL_ERROR);
    assert(Board_LEDC_Init(1025U) == HAL_ERROR);
    assert(RCC_LEDC_CLK == 0U);
    assert(Board_LEDC_Init(4U) == HAL_OK);
    LEDC_InitTypeDef valid = hledc.Init;
    uint32_t *fields[] = {&hledc.Init.T0H, &hledc.Init.T0L,         &hledc.Init.T1H,
                          &hledc.Init.T1L, &hledc.Init.ResetTimeUs, &hledc.Init.WaitDataTimeUs};
    const uint32_t maxima[] = {32U, 64U, 64U, 32U, 341U, 341U};
    for (uint32_t i = 0U; i < 6U; ++i)
    {
        *fields[i] = 0U;
        assert(HAL_LEDC_Init(&hledc) == HAL_ERROR);
        *fields[i] = maxima[i] + 1U;
        assert(HAL_LEDC_Init(&hledc) == HAL_ERROR);
        hledc.Init = valid;
    }
    assert(HAL_LEDC_Transmit(&hledc, NULL, 4U, 100U) == HAL_ERROR);
    assert(HAL_LEDC_Transmit(&hledc, colors, 0U, 100U) == HAL_ERROR);
    assert(HAL_LEDC_Transmit(&hledc, colors, 3U, 100U) == HAL_ERROR);
    hledc.Lock = HAL_LOCKED;
    assert(HAL_LEDC_Transmit(&hledc, colors, 4U, 100U) == HAL_BUSY);
    assert(HAL_LEDC_Init(&hledc) == HAL_BUSY);
    assert(HAL_LEDC_DeInit(&hledc) == HAL_BUSY);
    hledc.Lock = HAL_UNLOCKED;
    assert(HAL_LEDC_DeInit(&hledc) == HAL_OK);
    assert(hledc.State == HAL_LEDC_STATE_RESET);
    assert(((GPIOE->CFG[0] >> 8) & 7U) == 7U);
    assert(HAL_LEDC_Transmit(&hledc, colors, 4U, 100U) == HAL_ERROR);
    assert(HAL_RCC_LEDC_EnableClock(NULL) == HAL_ERROR);
    assert(HAL_RCC_LEDC_ForceReset(NULL) == HAL_ERROR);
}

static void test_errors_timeouts_and_recovery(void)
{
    for (uint32_t error = 8U; error <= 24U; error += 8U)
    {
        reset_model();
        assert(Board_LEDC_Init(4U) == HAL_OK);
        injected_error = error;
        assert(HAL_LEDC_Transmit(&hledc, colors, 4U, 100U) == HAL_ERROR);
        assert(hledc.ErrorCode == (((error & 8U) != 0U ? HAL_LEDC_ERROR_UNDERRUN : 0U) |
                                   ((error & 16U) != 0U ? HAL_LEDC_ERROR_FIFO : 0U)));
        assert(hledc.State == HAL_LEDC_STATE_RESET && hledc.Lock == HAL_UNLOCKED);
        assert((LEDC->CTRL & 1U) == 0U);
        assert(HAL_LEDC_Transmit(&hledc, colors, 4U, 100U) == HAL_ERROR);
        injected_error = 0U;
        assert(HAL_LEDC_Init(&hledc) == HAL_OK);
        assert(HAL_LEDC_Transmit(&hledc, colors, 4U, 100U) == HAL_OK);
    }
    reset_model();
    assert(Board_LEDC_Init(4U) == HAL_OK);
    stall = 1;
    tick = UINT32_MAX - 2U;
    assert(HAL_LEDC_Transmit(&hledc, colors, 4U, 10U) == HAL_TIMEOUT);
    assert(hledc.ErrorCode == HAL_LEDC_ERROR_TIMEOUT && hledc.State == HAL_LEDC_STATE_RESET);
    stall = 0;
    assert(HAL_LEDC_Init(&hledc) == HAL_OK);
    stuck_latch = 1;
    assert(HAL_LEDC_Transmit(&hledc, colors, 4U, 10U) == HAL_TIMEOUT);
    assert(hledc.State == HAL_LEDC_STATE_RESET && hledc.Lock == HAL_UNLOCKED);
    stuck_latch = 0;
    assert(HAL_LEDC_Init(&hledc) == HAL_OK);
    invalid_depth = 1;
    stall = 1;
    assert(HAL_LEDC_Transmit(&hledc, colors, 4U, 100U) == HAL_ERROR);
    assert(hledc.ErrorCode == HAL_LEDC_ERROR_FIFO);
    reset_model();
    stuck_reset = 1;
    assert(Board_LEDC_Init(4U) == HAL_TIMEOUT);
    assert(hledc.ErrorCode == HAL_LEDC_ERROR_RESET && hledc.Lock == HAL_UNLOCKED);
    assert(hledc.State == HAL_LEDC_STATE_RESET);
}

int main(void)
{
    const uintptr_t addresses[] = {R328_CCU_BASE, R328_GPIO_BASE, (uintptr_t)LEDC};
    for (uint32_t i = 0U; i < sizeof(addresses) / sizeof(addresses[0]); ++i)
    {
        assert(mmap((void *)addresses[i], 4096, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1,
                    0) == (void *)addresses[i]);
    }
    test_init_and_pins();
    test_frames_and_latch();
    test_validation_and_busy();
    test_errors_timeouts_and_recovery();
    puts("LEDC register, FIFO, timing, latch and recovery tests passed");
    return 0;
}
