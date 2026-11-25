#include "r328_hal_gpio.h"

static uint32_t valid_pins(const GPIO_TypeDef *port)
{
    if (port == GPIOB)
    {
        return 0x3FFFU;
    }
    if (port == GPIOC)
    {
        return 0x1801FU;
    } /* PC0..4, PC15..16 */
    if (port == GPIOE)
    {
        return 0x7FU;
    }
    if (port == GPIOG)
    {
        return 0x7FFFU;
    }
    if (port == GPIOH)
    {
        return 0x3FFU;
    }
    return 0U;
}

HAL_StatusTypeDef HAL_GPIO_Init(GPIO_TypeDef *port, const GPIO_InitTypeDef *init)
{
    if (init == NULL || init->Pin == 0U || (init->Pin & ~valid_pins(port)) != 0U ||
        init->Pull > GPIO_PULLDOWN || init->Drive > GPIO_DRIVE_LEVEL3)
    {
        return HAL_ERROR;
    }
    uint32_t mux = init->Mode;
    if (mux == GPIO_MODE_AF_PP)
    {
        if (init->Alternate < 2U || init->Alternate > 6U)
        {
            return HAL_ERROR;
        }
        mux = init->Alternate;
    }
    else if (mux != GPIO_MODE_INPUT && mux != GPIO_MODE_OUTPUT_PP && mux != GPIO_MODE_ANALOG)
    {
        return HAL_ERROR;
    }
    uint32_t state = R328_IRQ_Save();
    for (uint32_t pin = 0U; pin <= 16U; ++pin)
    {
        if ((init->Pin & (1UL << pin)) == 0U)
        {
            continue;
        }
        uint32_t cfg_shift = (pin % 8U) * 4U;
        uint32_t io_shift = (pin % 16U) * 2U;
        uint32_t index = pin / 16U;
        /* Preserve the reserved fourth bit in each mux field. */
        port->CFG[pin / 8U] = (port->CFG[pin / 8U] & ~(7UL << cfg_shift)) | (mux << cfg_shift);
        port->DRV[index] = (port->DRV[index] & ~(3UL << io_shift)) | (init->Drive << io_shift);
        port->PUL[index] = (port->PUL[index] & ~(3UL << io_shift)) | (init->Pull << io_shift);
    }
    R328_DSB();
    R328_IRQ_Restore(state);
    return HAL_OK;
}

HAL_StatusTypeDef HAL_GPIO_DeInit(GPIO_TypeDef *port, uint32_t pins)
{
    GPIO_InitTypeDef init = {.Pin = pins, .Mode = GPIO_MODE_ANALOG};
    return HAL_GPIO_Init(port, &init);
}
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint32_t pins, GPIO_PinState value)
{
    if (pins == 0U || (pins & ~valid_pins(port)) != 0U)
    {
        return;
    }
    uint32_t state = R328_IRQ_Save();
    if (value == GPIO_PIN_SET)
    {
        port->DAT |= pins;
    }
    else
    {
        port->DAT &= ~pins;
    }
    R328_DSB();
    R328_IRQ_Restore(state);
}
void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint32_t pins)
{
    if (pins == 0U || (pins & ~valid_pins(port)) != 0U)
    {
        return;
    }
    uint32_t state = R328_IRQ_Save();
    port->DAT ^= pins;
    R328_DSB();
    R328_IRQ_Restore(state);
}
GPIO_PinState HAL_GPIO_ReadPin(const GPIO_TypeDef *port, uint32_t pins)
{
    if (pins == 0U || (pins & ~valid_pins(port)) != 0U)
    {
        return GPIO_PIN_RESET;
    }
    return (port->DAT & pins) != 0U ? GPIO_PIN_SET : GPIO_PIN_RESET;
}
