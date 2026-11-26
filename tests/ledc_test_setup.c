/* Host-test peripheral setup; never linked into firmware. */
#include "ledc_test_setup.h"

LEDC_HandleTypeDef hledc;

HAL_StatusTypeDef Test_LEDC_Init(uint32_t led_count)
{
    hledc.Instance = LEDC;
    /* 24 MHz: each bit is 30 cycles (1.25 us), short pulse 7 cycles
     * (292 ns), long pulse 23 cycles (958 ns). XL-5050RGBC-WS2812B
     * specifies short 200..350 ns, long 550..1200 ns and reset >=80 us. */
    hledc.Init = (LEDC_InitTypeDef){.LEDCount = led_count,
                                    .T0H = 7U,
                                    .T0L = 23U,
                                    .T1H = 23U,
                                    .T1L = 7U,
                                    .ResetTimeUs = 300U,
                                    .WaitDataTimeUs = 50U};
    return HAL_LEDC_Init(&hledc);
}

void HAL_LEDC_MspInit(LEDC_HandleTypeDef *handle)
{
    if (handle->Instance == LEDC)
    {
        /* PE2 AF2 -> WS2812 DIN. Preload low before selecting LEDC output. */
        HAL_GPIO_WritePin(GPIOE, GPIO_PIN_2, GPIO_PIN_RESET);
        GPIO_InitTypeDef init = {.Pin = GPIO_PIN_2,
                                 .Mode = GPIO_MODE_AF_PP,
                                 .Pull = GPIO_NOPULL,
                                 .Drive = GPIO_DRIVE_LEVEL1,
                                 .Alternate = 2U};
        if (HAL_GPIO_Init(GPIOE, &init) != HAL_OK)
        {
            Error_Handler();
        }
    }
}
void HAL_LEDC_MspDeInit(LEDC_HandleTypeDef *handle)
{
    if (handle->Instance == LEDC)
    {
        (void)HAL_GPIO_DeInit(GPIOE, GPIO_PIN_2);
    }
}