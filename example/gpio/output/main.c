#include "example.h"
#include "gpio_example.h"

int main(void)
{
    Example_Init("GPIO / write, read back and toggle");
    static const GPIO_InitTypeDef output = {.Pin = EXAMPLE_GPIO_OUTPUT_PIN,
                                            .Mode = GPIO_MODE_OUTPUT_PP,
                                            .Pull = GPIO_NOPULL,
                                            .Drive = GPIO_DRIVE_LEVEL1};
    /* Preload the level before selecting the output function. */
    HAL_GPIO_WritePin(EXAMPLE_GPIO_OUTPUT_PORT, EXAMPLE_GPIO_OUTPUT_PIN, GPIO_PIN_RESET);
    Example_RequireOK("GPIO init=", HAL_GPIO_Init(EXAMPLE_GPIO_OUTPUT_PORT, &output));
    for (;;)
    {
        HAL_GPIO_WritePin(EXAMPLE_GPIO_OUTPUT_PORT, EXAMPLE_GPIO_OUTPUT_PIN, GPIO_PIN_SET);
        Example_PrintValue("write high, read=",
                           HAL_GPIO_ReadPin(EXAMPLE_GPIO_OUTPUT_PORT, EXAMPLE_GPIO_OUTPUT_PIN));
        HAL_Delay(500U);
        HAL_GPIO_WritePin(EXAMPLE_GPIO_OUTPUT_PORT, EXAMPLE_GPIO_OUTPUT_PIN, GPIO_PIN_RESET);
        Example_PrintValue("write low, read=",
                           HAL_GPIO_ReadPin(EXAMPLE_GPIO_OUTPUT_PORT, EXAMPLE_GPIO_OUTPUT_PIN));
        HAL_Delay(500U);
        HAL_GPIO_TogglePin(EXAMPLE_GPIO_OUTPUT_PORT, EXAMPLE_GPIO_OUTPUT_PIN);
        Example_PrintValue("toggle, read=",
                           HAL_GPIO_ReadPin(EXAMPLE_GPIO_OUTPUT_PORT, EXAMPLE_GPIO_OUTPUT_PIN));
        HAL_Delay(500U);
    }
}
