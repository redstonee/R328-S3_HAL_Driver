#include "r328_hal_bus_private.h"

static DMA_HandleTypeDef *owners[10];
static uint32_t irq_mask(uint32_t channel)
{
    return 7UL << ((channel % 8U) * 4U);
}
static void dma_irq(void *context)
{
    UNUSED(context);
    for (uint32_t i = 0U; i < 10U; ++i)
    {
        if (owners[i] != NULL)
        {
            HAL_DMA_IRQHandler(owners[i]);
        }
    }
}
static int owned(DMA_HandleTypeDef *h)
{
    return h != NULL && h->Init.Channel < 10U && owners[h->Init.Channel] == h;
}
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *h)
{
    if (h == NULL || h->Init.Channel >= 10U || h->Init.Direction > DMA_PERIPH_TO_MEMORY ||
        h->Init.Request >= 64U)
    {
        return HAL_ERROR;
    }
    uint32_t state = R328_IRQ_Save(), channel = h->Init.Channel;
    for (uint32_t i = 0U; i < 10U; ++i)
    {
        if (owners[i] == h && i != channel)
        {
            R328_IRQ_Restore(state);
            return HAL_BUSY; /* DeInit before moving a handle to another channel. */
        }
    }
    if ((owners[channel] != NULL && owners[channel] != h) || h->State == HAL_DMA_STATE_BUSY)
    {
        R328_IRQ_Restore(state);
        return HAL_BUSY;
    }
    /* Do not reset the controller: other channels may be active. */
    BUS_WRITE(RCC_DMA_BGR, BUS_READ(RCC_DMA_BGR) | (1UL << 16));
    R328_DSB();
    BUS_WRITE(RCC_DMA_BGR, BUS_READ(RCC_DMA_BGR) | 1U);
    BUS_WRITE(RCC_MBUS_MAT_CLK_GATING, BUS_READ(RCC_MBUS_MAT_CLK_GATING) | 1U);
    R328_DSB();
    BUS_WRITE(DMA->AUTO_GATE, BUS_READ(DMA->AUTO_GATE) | 4U);
    if ((BUS_READ(DMA->CHANNEL[channel].EN) & 1U) != 0U ||
        (BUS_READ(DMA->STATUS) & (1UL << channel)) != 0U)
    {
        R328_IRQ_Restore(state);
        return HAL_BUSY;
    }
    HAL_StatusTypeDef result = HAL_GIC_RegisterIRQ(DMA_IRQn, dma_irq, NULL, 0x90U);
    if (result == HAL_OK)
    {
        result = HAL_GIC_EnableIRQ(DMA_IRQn);
    }
    if (result == HAL_OK)
    {
        uint32_t bank = channel / 8U, mask = irq_mask(channel);
        BUS_WRITE(DMA->IRQ_EN[bank], BUS_READ(DMA->IRQ_EN[bank]) & ~mask);
        BUS_WRITE(DMA->IRQ_PEND[bank], mask);
#if defined(R328_GIC_NONSECURE)
        BUS_WRITE(DMA->SEC, BUS_READ(DMA->SEC) | (1UL << channel));
#else
        BUS_WRITE(DMA->SEC, BUS_READ(DMA->SEC) & ~(1UL << channel));
#endif
        owners[channel] = h;
        h->State = HAL_DMA_STATE_READY;
        h->ErrorCode = HAL_DMA_ERROR_NONE;
    }
    R328_IRQ_Restore(state);
    return result;
}
static int memory_port(uintptr_t address, uint32_t size)
{
    if (address >= 0x20000U && address < 0x40000U && size <= 0x40000U - address)
    {
        return 0;
    }
    if (address >= 0x40000000U && address < 0x48000000U && size <= 0x48000000U - address)
    {
        return 1;
    }
    return -1;
}
HAL_StatusTypeDef HAL_DMA_Start_IT(DMA_HandleTypeDef *h, uintptr_t src, uintptr_t dst,
                                   uint32_t size)
{
    if (!owned(h) || size == 0U || size > 0x1FFFFFFU || src > UINT32_MAX || dst > UINT32_MAX)
    {
        return HAL_ERROR;
    }
    int to_memory = h->Init.Direction == DMA_PERIPH_TO_MEMORY;
    int port = memory_port(to_memory ? dst : src, size);
    if (port < 0)
    {
        return HAL_ERROR;
    }
    uint32_t state = R328_IRQ_Save();
    if (h->State != HAL_DMA_STATE_READY)
    {
        R328_IRQ_Restore(state);
        return HAL_BUSY;
    }
    uint32_t channel = h->Init.Channel, bank = channel / 8U, mask = irq_mask(channel);
    h->Descriptor[0] = to_memory ? (uint32_t)port << 16 | 1U << 8 | h->Init.Request
                                 : 1UL << 24 | h->Init.Request << 16 | (uint32_t)port;
    h->Descriptor[1] = (uint32_t)src;
    h->Descriptor[2] = (uint32_t)dst;
    h->Descriptor[3] = size;
    h->Descriptor[4] = 8U;
    h->Descriptor[5] = 0xFFFFF800U;
    h->ErrorCode = HAL_DMA_ERROR_NONE;
    h->State = HAL_DMA_STATE_BUSY;
    BUS_WRITE(DMA->IRQ_PEND[bank], mask);
    BUS_WRITE(DMA->IRQ_EN[bank],
              (BUS_READ(DMA->IRQ_EN[bank]) & ~mask) | (4UL << ((channel % 8U) * 4U)));
    BUS_WRITE(DMA->CHANNEL[channel].PAUSE, 0U);
    /* Handshake each peripheral request. A fixed wait count can sample a stale
     * DRQ before the peripheral FIFO has refreshed, reading an empty RX FIFO
     * (or overflowing TX). Memory requests are permanently ready. */
    BUS_WRITE(DMA->CHANNEL[channel].MODE, (1U << 2) | (1U << 3));
    BUS_WRITE(DMA->CHANNEL[channel].DESC, (uint32_t)(uintptr_t)h->Descriptor);
    /* Physical addresses and caches disabled, as in the startup code. */
    R328_DSB();
    BUS_WRITE(DMA->CHANNEL[channel].EN, 1U);
    R328_IRQ_Restore(state);
    return HAL_OK;
}
HAL_StatusTypeDef HAL_DMA_Abort(DMA_HandleTypeDef *h)
{
    if (!owned(h))
    {
        return HAL_ERROR;
    }
    uint32_t state = R328_IRQ_Save(), channel = h->Init.Channel, bank = channel / 8U;
    BUS_WRITE(DMA->IRQ_EN[bank], BUS_READ(DMA->IRQ_EN[bank]) & ~irq_mask(channel));
    BUS_WRITE(DMA->CHANNEL[channel].EN, 0U);
    R328_DSB();
    HAL_StatusTypeDef result = HAL_TIMEOUT;
    for (uint32_t i = 0U; i < HAL_REGISTER_POLL_LIMIT; ++i)
    {
        if ((BUS_READ(DMA->STATUS) & (1UL << channel)) == 0U &&
            (BUS_READ(DMA->CHANNEL[channel].EN) & 1U) == 0U)
        {
            result = HAL_OK;
            break;
        }
    }
    BUS_WRITE(DMA->IRQ_PEND[bank], irq_mask(channel));
    R328_DSB();
    h->State = result == HAL_OK ? HAL_DMA_STATE_READY : HAL_DMA_STATE_ERROR;
    h->ErrorCode = result == HAL_OK ? HAL_DMA_ERROR_NONE : HAL_DMA_ERROR_TIMEOUT;
    R328_IRQ_Restore(state);
    return result;
}
HAL_StatusTypeDef HAL_DMA_DeInit(DMA_HandleTypeDef *h)
{
    HAL_StatusTypeDef result = HAL_DMA_Abort(h);
    if (result == HAL_OK)
    {
        uint32_t state = R328_IRQ_Save();
        owners[h->Init.Channel] = NULL;
        h->State = HAL_DMA_STATE_RESET;
        R328_IRQ_Restore(state);
    }
    return result;
}
void HAL_DMA_IRQHandler(DMA_HandleTypeDef *h)
{
    if (!owned(h))
    {
        return;
    }
    uint32_t channel = h->Init.Channel, bank = channel / 8U;
    uint32_t pending = BUS_READ(DMA->IRQ_PEND[bank]) & irq_mask(channel);
    if (pending == 0U)
    {
        return;
    }
    BUS_WRITE(DMA->IRQ_PEND[bank], pending);
    if (h->State == HAL_DMA_STATE_BUSY && (pending & (4UL << ((channel % 8U) * 4U))) != 0U)
    {
        BUS_WRITE(DMA->IRQ_EN[bank], BUS_READ(DMA->IRQ_EN[bank]) & ~irq_mask(channel));
        BUS_WRITE(DMA->CHANNEL[channel].EN, 0U);
        R328_DSB();
        h->ErrorCode = BUS_READ(DMA->CHANNEL[channel].LEFT) == 0U ? HAL_DMA_ERROR_NONE
                                                                  : HAL_DMA_ERROR_TRANSFER;
        h->State = h->ErrorCode == HAL_DMA_ERROR_NONE ? HAL_DMA_STATE_READY : HAL_DMA_STATE_ERROR;
        if (h->XferCpltCallback != NULL)
        {
            h->XferCpltCallback(h);
        }
    }
}
