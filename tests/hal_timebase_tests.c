/* Exercise the real HAL timebase with a running/stalled physical-counter model.
 * No timer driver, periodic callback or MMIO mapping is linked into this test. */
#include "r328_hal.h"
#include <assert.h>
#include <stdio.h>

static uint64_t physical_count;
static uint64_t read_step;
static uint32_t read_calls;
static HAL_StatusTypeDef gic_status = HAL_ERROR;

uint64_t R328_Test_ArchCounter_Read(void)
{
    uint64_t sample = physical_count;
    physical_count += read_step;
    ++read_calls;
    return sample;
}

HAL_StatusTypeDef HAL_GIC_Init(void)
{
    return gic_status;
}

int main(void)
{
    assert(HAL_GetTick() == 0U);
    HAL_Delay(0U);
    assert(read_calls == 0U);

    /* GIC failure must propagate; a stopped counter must not hang startup. */
    assert(HAL_Init() == HAL_ERROR);
    assert(read_calls == 0U);
    gic_status = HAL_OK;
    physical_count = 123456789U;
    assert(HAL_Init() == HAL_TIMEOUT);
    assert(HAL_GetTick() == 0U);

    /* A high, nonzero bootloader count must still yield a zero-based HAL tick.
     * Cross the low 32-bit boundary during the first millisecond. */
    const uint64_t epoch = UINT64_C(0x10fffffff0);
    physical_count = epoch;
    read_step = 1U;
    assert(HAL_InitTick() == HAL_OK);
    read_step = 0U;
    assert(HAL_GetTick() == 0U);
    uint32_t calls_before = read_calls;
    assert(HAL_InitTick() == HAL_BUSY);
    assert(read_calls == calls_before);

    physical_count = epoch + 23999U;
    assert(HAL_GetTick() == 0U);
    physical_count = epoch + 24000U;
    assert(HAL_GetTick() == 1U);
    physical_count = epoch + 24000000U;
    assert(HAL_GetTick() == 1000U);

    /* No calls at all for ten seconds: hardware elapsed time must be retained,
     * as with IRQs masked or a long foreground transfer. */
    physical_count += 240000000U;
    assert(HAL_GetTick() == 11000U);
    HAL_IncTick();
    assert(HAL_GetTick() == 11000U);

    /* Pass the raw low-word rollover (~179 s at 24 MHz) and 49.7 day HAL wrap. */
    physical_count = epoch + UINT64_C(4800000000);
    assert(HAL_GetTick() == 200000U);
    physical_count = epoch + UINT64_C(0xffffffff) * 24000U;
    assert(HAL_GetTick() == UINT32_MAX);
    physical_count += 24000U;
    assert(HAL_GetTick() == 0U);
    physical_count += 24000U;
    assert(HAL_GetTick() == 1U);

    /* Delay still completes across the millisecond wrap without an IRQ source.
     * Each read advances simulated wall time by 250 us. */
    physical_count = epoch + UINT64_C(0xfffffffe) * 24000U;
    uint64_t before_delay = physical_count;
    read_step = 6000U;
    HAL_Delay(5U);
    read_step = 0U;
    assert(physical_count - before_delay >= 120000U);
    assert(physical_count - before_delay <= 132000U);
    assert(HAL_GetTick() >= 3U && HAL_GetTick() <= 4U);

    calls_before = read_calls;
    HAL_Delay(0U);
    assert(read_calls == calls_before);
    puts("Architectural counter HAL timebase checks passed");
    return 0;
}
