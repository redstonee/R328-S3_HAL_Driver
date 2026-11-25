#include "r328_hal_rcc.h"

static int uart_index(const UART_TypeDef *instance)
{
    if (instance == UART0)
    {
        return 0;
    }
    if (instance == UART1)
    {
        return 1;
    }
    if (instance == UART2)
    {
        return 2;
    }
    if (instance == UART3)
    {
        return 3;
    }
    return -1;
}
HAL_StatusTypeDef HAL_RCC_APB2_Config24M(void)
{
    uint32_t state = R328_IRQ_Save();
    RCC_APB2_CFG &= ~((3UL << 24) | (3UL << 8) | 3UL);
    R328_DSB();
    R328_IRQ_Restore(state);
    return HAL_OK;
}
uint32_t HAL_RCC_GetPCLK2Freq(void)
{
    uint32_t cfg = RCC_APB2_CFG;
    /* Other sources require board clock-tree support; never guess their rate. */
    if (((cfg >> 24) & 3U) != 0U)
    {
        return 0U;
    }
    return R328_OSC24M_HZ / ((cfg & 3U) + 1U) / (1UL << ((cfg >> 8) & 3U));
}
HAL_StatusTypeDef HAL_RCC_UART_EnableClock(UART_TypeDef *instance)
{
    int index = uart_index(instance);
    if (index < 0)
    {
        return HAL_ERROR;
    }
    uint32_t state = R328_IRQ_Save();
    RCC_UART_BGR |= 1UL << (uint32_t)index;
    R328_DSB();
    RCC_UART_BGR |= 1UL << (16U + (uint32_t)index);
    R328_DSB();
    R328_IRQ_Restore(state);
    return HAL_OK;
}
HAL_StatusTypeDef HAL_RCC_UART_ForceReset(UART_TypeDef *instance)
{
    int index = uart_index(instance);
    if (index < 0)
    {
        return HAL_ERROR;
    }
    uint32_t state = R328_IRQ_Save();
    RCC_UART_BGR &= ~(1UL << (16U + (uint32_t)index));
    R328_DSB();
    RCC_UART_BGR |= 1UL << (16U + (uint32_t)index);
    R328_DSB();
    R328_IRQ_Restore(state);
    return HAL_OK;
}
