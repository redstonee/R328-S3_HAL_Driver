/* Real GPIO/GIC code with mapped registers. STATUS assertions inspect issued
 * W1C masks; this RAM model does not emulate physical input edges or W1C. */
#include "r328_hal.h"
#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>

static uint32_t callbacks;
static uint32_t seen;
static GPIO_TypeDef *last_port;

void HAL_GPIO_EXTI_Callback(GPIO_TypeDef *port, uint32_t pin)
{
    assert(pin != 0U && (pin & (pin - 1U)) == 0U);
    ++callbacks;
    seen |= pin;
    last_port = port;
}
static void foreign_handler(void *context)
{
    UNUSED(context);
}
static void dispatch_irq(IRQn_Type irq)
{
    R328_REG32(R328_GICC_BASE + 0xCU) = (uint32_t)irq;
    HAL_GIC_IRQHandler();
    assert(R328_REG32(R328_GICC_BASE + 0x10U) == (uint32_t)irq);
}

int main(void)
{
    void *mapped = mmap((void *)0x03000000U, 0x30000U, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(mapped == (void *)0x03000000U);
    GPIO_EXTI_InitTypeDef input = {
        .Pin = GPIO_PIN_7, .Trigger = GPIO_EXTI_BOTH, .Pull = GPIO_PULLUP};
    assert(HAL_GPIO_EXTI_Init(GPIOB, &input) == HAL_ERROR); /* GIC not ready. */
    assert(GPIOB_EXTI->CTL == 0U && GPIOB->CFG[0] == 0U);
    R328_REG32(R328_GICD_BASE + 4U) = 5U;
    assert(HAL_GIC_Init() == HAL_OK);
    assert(HAL_GPIO_EXTI_Init(GPIOC, &input) == HAL_ERROR);
    assert(HAL_GPIO_EXTI_Init(NULL, &input) == HAL_ERROR);
    assert(HAL_GPIO_EXTI_Init(GPIOB, NULL) == HAL_ERROR);
    input.Pin = GPIO_PIN_14;
    assert(HAL_GPIO_EXTI_Init(GPIOB, &input) == HAL_ERROR);
    input.Pin = GPIO_PIN_7;
    input.Trigger = 5U;
    assert(HAL_GPIO_EXTI_Init(GPIOB, &input) == HAL_ERROR);
    input.Trigger = GPIO_EXTI_BOTH;
    assert(HAL_GIC_RegisterIRQ(GPIOB_IRQn, foreign_handler, NULL, 0x80U) == HAL_OK);
    assert(HAL_GPIO_EXTI_Init(GPIOB, &input) == HAL_BUSY);
    assert(GPIOB->CFG[0] == 0U && GPIOB_EXTI->CTL == 0U);
    assert(HAL_GIC_UnregisterIRQ(GPIOB_IRQn, NULL) == HAL_OK);
    GPIOB_EXTI->CTL = GPIO_PIN_1; /* Do not take over an unowned active pin. */
    assert(HAL_GPIO_EXTI_Init(GPIOB, &input) == HAL_BUSY);
    GPIOB_EXTI->CTL = 0U;
    GPIOB->CFG[0] = 0x80000003U;
    GPIOB_EXTI->CFG[0] = 0xA1234567U;
    GPIOB_EXTI->DEB = 0xFFFF0070U;
    assert(HAL_GPIO_EXTI_Init(GPIOB, &input) == HAL_OK);
    assert(GPIOB->CFG[0] == 0xE0000003U); /* Preserve reserved mux bit and PB0. */
    assert(GPIOB_EXTI->CFG[0] == 0x41234567U);
    assert(GPIOB_EXTI->CTL == GPIO_PIN_7 && GPIOB_EXTI->STATUS == GPIO_PIN_7);
    assert(GPIOB_EXTI->DEB == 0xFFFF0001U);
    assert(((GPIOB->PUL[0] >> 14) & 3U) == GPIO_PULLUP);
    assert(HAL_GPIO_EXTI_Init(GPIOB, &input) == HAL_BUSY);
    input.Pin = GPIO_PIN_8;
    input.Trigger = GPIO_EXTI_FALLING;
    assert(HAL_GPIO_EXTI_Init(GPIOB, &input) == HAL_OK);
    assert((GPIOB_EXTI->CFG[1] & 0xFU) == GPIO_EXTI_FALLING);
    GPIOB_EXTI->STATUS = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_2;
    dispatch_irq(GPIOB_IRQn);
    assert(callbacks == 2U && seen == (GPIO_PIN_7 | GPIO_PIN_8) && last_port == GPIOB);
    assert(GPIOB_EXTI->STATUS == (GPIO_PIN_7 | GPIO_PIN_8)); /* W1C only owned, enabled bits. */
    GPIOB_EXTI->CTL &= ~GPIO_PIN_8;
    GPIOB_EXTI->STATUS = GPIO_PIN_8;
    dispatch_irq(GPIOB_IRQn);
    assert(callbacks == 2U && GPIOB_EXTI->STATUS == GPIO_PIN_8);
    GPIOB_EXTI->CTL |= GPIO_PIN_8;
    assert(HAL_GPIO_EXTI_DeInit(GPIOB, GPIO_PIN_7 | GPIO_PIN_9) == HAL_ERROR);
    assert(HAL_GPIO_EXTI_DeInit(GPIOB, GPIO_PIN_7) == HAL_OK);
    assert(GPIOB_EXTI->CTL == GPIO_PIN_8);
    assert(HAL_GIC_RegisterIRQ(GPIOB_IRQn, foreign_handler, NULL, 0x80U) == HAL_BUSY);
    GPIOB_EXTI->STATUS = GPIO_PIN_8;
    dispatch_irq(GPIOB_IRQn);
    assert(callbacks == 3U);
    assert(HAL_GPIO_EXTI_DeInit(GPIOB, GPIO_PIN_8) == HAL_OK);
    assert(GPIOB_EXTI->CTL == 0U && HAL_GIC_EnableIRQ(GPIOB_IRQn) == HAL_ERROR);
    assert(HAL_GPIO_EXTI_DeInit(GPIOB, GPIO_PIN_8) == HAL_ERROR);
    assert(HAL_GPIO_EXTI_DeInit(NULL, GPIO_PIN_8) == HAL_ERROR);
    assert(HAL_GPIO_EXTI_DeInit(GPIOB, 0U) == HAL_ERROR);
    GPIOB_EXTI->STATUS = GPIO_PIN_7;
    HAL_GPIO_EXTI_IRQHandler(GPIOB);
    HAL_GPIO_EXTI_IRQHandler(GPIOC);
    assert(callbacks == 3U);

    GPIO_TypeDef *ports[] = {GPIOE, GPIOG, GPIOH};
    GPIO_EXTI_TypeDef *registers[] = {GPIOE_EXTI, GPIOG_EXTI, GPIOH_EXTI};
    IRQn_Type irqs[] = {GPIOE_IRQn, GPIOG_IRQn, GPIOH_IRQn};
    for (uint32_t i = 0U; i < 3U; ++i)
    {
        input.Pin = GPIO_PIN_0;
        input.Trigger = GPIO_EXTI_RISING;
        assert(HAL_GPIO_EXTI_Init(ports[i], &input) == HAL_OK);
        assert(registers[i]->CTL == GPIO_PIN_0 && (ports[i]->CFG[0] & 7U) == 6U);
        registers[i]->STATUS = GPIO_PIN_0;
        dispatch_irq(irqs[i]);
        assert(last_port == ports[i]);
        assert(HAL_GPIO_EXTI_DeInit(ports[i], GPIO_PIN_0) == HAL_OK);
    }
    assert(callbacks == 6U);
    puts("GPIO EINT banks, GIC dispatch, shared-pin ownership and W1C masks passed");
    return 0;
}
