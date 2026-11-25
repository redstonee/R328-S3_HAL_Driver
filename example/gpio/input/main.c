#include "example.h"
#include "gpio_example.h"

int main(void)
{
    Example_Init("GPIO / input with pull-up and software debounce");
    static const GPIO_InitTypeDef input = {.Pin = EXAMPLE_GPIO_INPUT_PIN,
                                           .Mode = GPIO_MODE_INPUT,
                                           .Pull = GPIO_PULLUP,
                                           .Drive = GPIO_DRIVE_LEVEL0};
    Example_RequireOK("GPIO init=", HAL_GPIO_Init(EXAMPLE_GPIO_INPUT_PORT, &input));
    GPIO_PinState candidate = HAL_GPIO_ReadPin(EXAMPLE_GPIO_INPUT_PORT, EXAMPLE_GPIO_INPUT_PIN);
    GPIO_PinState stable = candidate;
    uint32_t changed = HAL_GetTick();
    Example_PrintValue("initial level=", stable);
    for (;;)
    {
        GPIO_PinState sample = HAL_GPIO_ReadPin(EXAMPLE_GPIO_INPUT_PORT, EXAMPLE_GPIO_INPUT_PIN);
        uint32_t now = HAL_GetTick();
        if (sample != candidate)
        {
            candidate = sample;
            changed = now;
        }
        if (candidate != stable && (uint32_t)(now - changed) >= EXAMPLE_GPIO_DEBOUNCE_MS)
        {
            stable = candidate;
            Example_PrintValue("stable level=", stable);
        }
        Example_UART_Poll();
    }
}
