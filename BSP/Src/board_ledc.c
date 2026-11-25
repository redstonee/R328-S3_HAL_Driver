#include "board.h"

LEDC_HandleTypeDef hledc;

HAL_StatusTypeDef Board_LEDC_Init(uint32_t led_count)
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
