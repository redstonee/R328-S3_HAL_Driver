#include "r328_hal_gpio.h"
#include "r328_hal_gic.h"

typedef struct
{
    GPIO_TypeDef *port;
    GPIO_EXTI_TypeDef *registers;
    IRQn_Type irq;
    uint32_t valid;
    uint32_t active;
} EXTI_Bank;

static EXTI_Bank banks[] = {
    {GPIOB, GPIOB_EXTI, GPIOB_IRQn, 0x3FFFU, 0U},
    {GPIOE, GPIOE_EXTI, GPIOE_IRQn, 0x7FU, 0U},
    {GPIOG, GPIOG_EXTI, GPIOG_IRQn, 0x7FFFU, 0U},
    {GPIOH, GPIOH_EXTI, GPIOH_IRQn, 0x3FFU, 0U},
};

static EXTI_Bank *find_bank(const GPIO_TypeDef *port)
{
    for (uint32_t i = 0U; i < sizeof(banks) / sizeof(banks[0]); ++i)
    {
        if (banks[i].port == port)
        {
            return &banks[i];
        }
    }
    return NULL;
}

static void dispatch(void *context)
{
    EXTI_Bank *bank = context;
    HAL_GPIO_EXTI_IRQHandler(bank->port);
}

HAL_StatusTypeDef HAL_GPIO_EXTI_Init(GPIO_TypeDef *port, const GPIO_EXTI_InitTypeDef *init)
{
    EXTI_Bank *bank = find_bank(port);
    if (bank == NULL || init == NULL || init->Pin == 0U || (init->Pin & ~bank->valid) != 0U ||
        init->Trigger > GPIO_EXTI_BOTH || init->Pull > GPIO_PULLDOWN)
    {
        return HAL_ERROR;
    }
    uint32_t state = R328_IRQ_Save();
    if ((bank->active & init->Pin) != 0U ||
        (bank->registers->CTL & bank->valid & ~bank->active) != 0U)
    {
        R328_IRQ_Restore(state);
        return HAL_BUSY;
    }
    uint32_t first = bank->active == 0U;
    HAL_StatusTypeDef status = HAL_GIC_RegisterIRQ(bank->irq, dispatch, bank, 0xA0U);
    if (status == HAL_OK)
    {
        /* Peripheral enables remain clear while the GIC bank line is enabled. */
        status = HAL_GIC_EnableIRQ(bank->irq);
        if (status != HAL_OK && first != 0U)
        {
            (void)HAL_GIC_UnregisterIRQ(bank->irq, bank);
        }
    }
    if (status != HAL_OK)
    {
        R328_IRQ_Restore(state);
        return status;
    }
    GPIO_InitTypeDef gpio = {.Pin = init->Pin,
                             .Mode = GPIO_MODE_AF_PP,
                             .Pull = init->Pull,
                             .Drive = GPIO_DRIVE_LEVEL0,
                             .Alternate = 6U};
    status = HAL_GPIO_Init(port, &gpio);
    if (status != HAL_OK)
    {
        if (first != 0U)
        {
            (void)HAL_GIC_UnregisterIRQ(bank->irq, bank);
        }
        R328_IRQ_Restore(state);
        return status;
    }
    if (first != 0U)
    {
        /* Shared per-bank sampling clock: HOSC24M / 1, no slow-clock dependency.
         * Mechanical switch debounce is an application-level policy. */
        bank->registers->DEB = (bank->registers->DEB & ~0x71UL) | 1U;
    }
    for (uint32_t pin = 0U; pin < 16U; ++pin)
    {
        if ((init->Pin & (1UL << pin)) != 0U)
        {
            uint32_t shift = (pin % 8U) * 4U;
            bank->registers->CFG[pin / 8U] =
                (bank->registers->CFG[pin / 8U] & ~(0xFUL << shift)) | (init->Trigger << shift);
        }
    }
    bank->registers->STATUS = init->Pin; /* W1C; do not clear another pin. */
    bank->active |= init->Pin;
    bank->registers->CTL |= init->Pin;
    R328_DSB();
    R328_IRQ_Restore(state);
    return HAL_OK;
}

HAL_StatusTypeDef HAL_GPIO_EXTI_DeInit(GPIO_TypeDef *port, uint32_t pins)
{
    EXTI_Bank *bank = find_bank(port);
    if (bank == NULL || pins == 0U || (pins & ~bank->valid) != 0U)
    {
        return HAL_ERROR;
    }
    uint32_t state = R328_IRQ_Save();
    if ((bank->active & pins) != pins)
    {
        R328_IRQ_Restore(state);
        return HAL_ERROR;
    }
    bank->registers->CTL &= ~pins;
    bank->registers->STATUS = pins;
    bank->active &= ~pins;
    HAL_StatusTypeDef status = HAL_GPIO_DeInit(port, pins);
    if (bank->active == 0U)
    {
        HAL_StatusTypeDef release = HAL_GIC_UnregisterIRQ(bank->irq, bank);
        if (status == HAL_OK)
        {
            status = release;
        }
    }
    R328_DSB();
    R328_IRQ_Restore(state);
    return status;
}

void HAL_GPIO_EXTI_IRQHandler(GPIO_TypeDef *port)
{
    EXTI_Bank *bank = find_bank(port);
    if (bank == NULL)
    {
        return;
    }
    uint32_t pending = bank->registers->STATUS & bank->registers->CTL & bank->active;
    if (pending == 0U)
    {
        return;
    }
    bank->registers->STATUS = pending;
    R328_DSB();
    for (uint32_t pin = 0U; pin < 16U; ++pin)
    {
        if ((pending & (1UL << pin)) != 0U)
        {
            HAL_GPIO_EXTI_Callback(port, 1UL << pin);
        }
    }
}

__weak void HAL_GPIO_EXTI_Callback(GPIO_TypeDef *port, uint32_t pin)
{
    UNUSED(port);
    UNUSED(pin);
}
