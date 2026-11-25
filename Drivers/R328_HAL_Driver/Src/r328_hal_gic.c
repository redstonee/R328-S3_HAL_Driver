#include "r328_hal_gic.h"

#define GICD(offset) R328_REG32(R328_GICD_BASE + (offset))
#define GICC(offset) R328_REG32(R328_GICC_BASE + (offset))
typedef struct
{
    HAL_GIC_HandlerTypeDef handler;
    void *context;
} IRQ_Entry;
static IRQ_Entry irq_table[R328_IRQ_COUNT];
static uint32_t initialized;

static int valid_irq(IRQn_Type irq)
{
    return (uint32_t)irq >= 32U && (uint32_t)irq < R328_IRQ_COUNT;
}
HAL_StatusTypeDef HAL_GIC_Init(void)
{
    if (initialized != 0U)
    {
        return HAL_BUSY;
    }
    uint32_t state = R328_IRQ_Save();
    uint32_t lines = ((GICD(0x004U) & 31U) + 1U) * 32U;
    if (lines < R328_IRQ_COUNT)
    {
        R328_IRQ_Restore(state);
        return HAL_ERROR;
    }
    GICC(0x000U) = 0U;
    GICD(0x000U) = 0U;
    R328_DSB();
    /* NS accesses operate only on interrupts already assigned to Group 1 by
       secure firmware; NS software cannot change security groups. */
    for (uint32_t index = 0U; index < (R328_IRQ_COUNT + 31U) / 32U; ++index)
    {
        GICD(0x180U + index * 4U) = UINT32_MAX;
        GICD(0x280U + index * 4U) = UINT32_MAX;
        GICD(0x380U + index * 4U) = UINT32_MAX;
#if !defined(R328_GIC_NONSECURE)
        GICD(0x080U + index * 4U) = 0U;
#endif
    }
    for (uint32_t irq = 32U; irq < R328_IRQ_COUNT; ++irq)
    {
        *(__IO uint8_t *)(uintptr_t)(R328_GICD_BASE + 0x400U + irq) = 0xA0U;
        *(__IO uint8_t *)(uintptr_t)(R328_GICD_BASE + 0x800U + irq) = 1U; /* CPU0 */
        uint32_t shift = (irq % 16U) * 2U;
        GICD(0xC00U + irq / 16U * 4U) &= ~(3UL << shift); /* level-sensitive */
    }
    GICC(0x004U) = 0xFFU;
    GICC(0x008U) = 0U;
    GICD(0x000U) = 1U;
    GICC(0x000U) = 1U; /* Secure: Group0 IRQ; NS alias: Group1. EOImode=0. */
    R328_DSB();
    initialized = 1U;
    R328_IRQ_Restore(state);
    return HAL_OK;
}
HAL_StatusTypeDef HAL_GIC_RegisterIRQ(IRQn_Type irq, HAL_GIC_HandlerTypeDef handler, void *context,
                                      uint8_t priority)
{
    if (!valid_irq(irq) || handler == NULL || initialized == 0U)
    {
        return HAL_ERROR;
    }
    uint32_t state = R328_IRQ_Save();
    IRQ_Entry *entry = &irq_table[(uint32_t)irq];
    if (entry->handler != NULL && (entry->context != context || entry->handler != handler))
    {
        R328_IRQ_Restore(state);
        return HAL_BUSY;
    }
    entry->context = context;
    entry->handler = handler;
    *(__IO uint8_t *)(uintptr_t)(R328_GICD_BASE + 0x400U + (uint32_t)irq) = priority;
    R328_DSB();
    R328_IRQ_Restore(state);
    return HAL_OK;
}
HAL_StatusTypeDef HAL_GIC_UnregisterIRQ(IRQn_Type irq, void *context)
{
    if (!valid_irq(irq) || initialized == 0U)
    {
        return HAL_ERROR;
    }
    uint32_t state = R328_IRQ_Save();
    IRQ_Entry *entry = &irq_table[(uint32_t)irq];
    if (entry->handler != NULL && entry->context != context)
    {
        R328_IRQ_Restore(state);
        return HAL_BUSY;
    }
    HAL_GIC_DisableIRQ(irq);
    entry->handler = NULL;
    entry->context = NULL;
    R328_IRQ_Restore(state);
    return HAL_OK;
}
HAL_StatusTypeDef HAL_GIC_EnableIRQ(IRQn_Type irq)
{
    if (!valid_irq(irq) || initialized == 0U || irq_table[(uint32_t)irq].handler == NULL)
    {
        return HAL_ERROR;
    }
    GICD(0x100U + (uint32_t)irq / 32U * 4U) = 1UL << ((uint32_t)irq % 32U);
    R328_DSB();
    return HAL_OK;
}
void HAL_GIC_DisableIRQ(IRQn_Type irq)
{
    if (!valid_irq(irq))
    {
        return;
    }
    GICD(0x180U + (uint32_t)irq / 32U * 4U) = 1UL << ((uint32_t)irq % 32U);
    R328_DSB();
}
void HAL_GIC_IRQHandler(void)
{
    uint32_t acknowledge = GICC(0x00CU);
    uint32_t irq = acknowledge & 0x3FFU;
    if (irq >= 1020U)
    {
        return;
    } /* Spurious IDs must not be EOI'ed. */
    if (irq < R328_IRQ_COUNT && irq_table[irq].handler != NULL)
    {
        irq_table[irq].handler(irq_table[irq].context);
    }
    else
    {
        if (irq >= 32U)
        {
            GICD(0x180U + irq / 32U * 4U) = 1UL << (irq % 32U);
        }
        HAL_GIC_UnhandledIRQ(irq);
    }
    R328_DSB();                 /* Peripheral W1C must reach the peripheral before EOI. */
    GICC(0x010U) = acknowledge; /* Preserve CPU ID bits for SGIs. */
    R328_DSB();
}
__weak void HAL_GIC_UnhandledIRQ(uint32_t irq)
{
    UNUSED(irq);
}
