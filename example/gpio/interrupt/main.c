#include "example.h"
#include "gpio_example.h"

static volatile uint32_t edges;
static volatile uint32_t last_edge;

void HAL_GPIO_EXTI_Callback(GPIO_TypeDef *port, uint32_t pin)
{
    if (port == EXAMPLE_GPIO_INPUT_PORT && pin == EXAMPLE_GPIO_INPUT_PIN)
    {
        ++edges;
        last_edge = HAL_GetTick();
    }
}

int main(void)
{
    Example_Init("GPIO / both-edge EINT with foreground debounce");
    static const GPIO_EXTI_InitTypeDef input = {
        .Pin = EXAMPLE_GPIO_INPUT_PIN, .Trigger = GPIO_EXTI_BOTH, .Pull = GPIO_PULLUP};
    Example_RequireOK("EXTI init=", HAL_GPIO_EXTI_Init(EXAMPLE_GPIO_INPUT_PORT, &input));
    (void)Board_ConsoleWrite("Change the input level; press q to release EINT.\r\n");
    uint32_t reported = 0U;
    for (;;)
    {
        /* Snapshot both IRQ-owned values in one critical section. */
        uint32_t state = R328_IRQ_Save();
        uint32_t count = edges;
        uint32_t changed = last_edge;
        R328_IRQ_Restore(state);
        if (count != reported && (uint32_t)(HAL_GetTick() - changed) >= EXAMPLE_GPIO_DEBOUNCE_MS)
        {
            reported = count;
            Example_PrintValue("raw edges=", count);
            Example_PrintValue("settled level=",
                               HAL_GPIO_ReadPin(EXAMPLE_GPIO_INPUT_PORT, EXAMPLE_GPIO_INPUT_PIN));
        }
        uint8_t byte;
        if (HAL_UART_Receive(&huart0, &byte, 1U, 0U) == HAL_OK && byte == 'q')
        {
            Example_RequireOK("EXTI deinit=", HAL_GPIO_EXTI_DeInit(EXAMPLE_GPIO_INPUT_PORT,
                                                                   EXAMPLE_GPIO_INPUT_PIN));
            (void)Board_ConsoleWrite("EINT released; input mux disabled.\r\n");
            Example_Idle();
        }
    }
}
