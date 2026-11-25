#include "main.h"

void HAL_MspInit(void) {}
void HAL_UART_MspInit(UART_HandleTypeDef *handle)
{
    if (handle->Instance != UART0)
    {
        return;
    }
    GPIO_InitTypeDef init = {
        .Mode = GPIO_MODE_AF_PP, .Pull = GPIO_PULLUP, .Drive = GPIO_DRIVE_LEVEL1};
#if defined(R328_UART0_ROUTE_PB8_PB9)
    init.Pin = GPIO_PIN_8 | GPIO_PIN_9;
    init.Alternate = 5U;
    if (HAL_GPIO_Init(GPIOB, &init) != HAL_OK)
    {
        Error_Handler();
    }
#elif defined(R328_UART0_ROUTE_PH0_PH1)
    init.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    init.Alternate = 3U;
    if (HAL_GPIO_Init(GPIOH, &init) != HAL_OK)
    {
        Error_Handler();
    }
#else
#error "Select a supported UART0 route"
#endif
}
void HAL_UART_MspDeInit(UART_HandleTypeDef *handle)
{
    if (handle->Instance != UART0)
    {
        return;
    }
#if defined(R328_UART0_ROUTE_PB8_PB9)
    (void)HAL_GPIO_DeInit(GPIOB, GPIO_PIN_8 | GPIO_PIN_9);
#else
    (void)HAL_GPIO_DeInit(GPIOH, GPIO_PIN_0 | GPIO_PIN_1);
#endif
}

/* Example SPI1 routing: ILI9341 (PH4 CS) and XPT2046
 * (PH8 GPIO CS). Both selects remain GPIOs so multi-call commands can keep CS. */
void HAL_SPI_MspInit(SPI_HandleTypeDef *handle)
{
    if (handle->Instance != SPI1)
    {
        return;
    }
    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_4 | GPIO_PIN_8, GPIO_PIN_SET);
    GPIO_InitTypeDef init = {.Pin = GPIO_PIN_4 | GPIO_PIN_8,
                             .Mode = GPIO_MODE_OUTPUT_PP,
                             .Pull = GPIO_PULLUP,
                             .Drive = GPIO_DRIVE_LEVEL1};
    if (HAL_GPIO_Init(GPIOH, &init) != HAL_OK)
    {
        Error_Handler();
    }
    init.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    init.Mode = GPIO_MODE_AF_PP;
    init.Alternate = 3U;
    init.Pull = GPIO_NOPULL;
    if (HAL_GPIO_Init(GPIOH, &init) != HAL_OK)
    {
        Error_Handler();
    }
}
void HAL_SPI_MspDeInit(SPI_HandleTypeDef *handle)
{
    if (handle->Instance != SPI1)
    {
        return;
    }
    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_4 | GPIO_PIN_8, GPIO_PIN_SET);
    (void)HAL_GPIO_DeInit(GPIOH, GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7);
}
void HAL_I2C_MspInit(I2C_HandleTypeDef *handle)
{
    if (handle->Instance != I2C1)
    {
        return;
    }
    GPIO_InitTypeDef init = {.Pin = GPIO_PIN_8 | GPIO_PIN_9,
                             .Mode = GPIO_MODE_AF_PP,
                             .Pull = GPIO_PULLUP,
                             .Drive = GPIO_DRIVE_LEVEL1,
                             .Alternate = 4U};
    /* TWI alternate function owns its open-drain output; GPIO has no OD bit. */
    if (HAL_GPIO_Init(GPIOB, &init) != HAL_OK)
    {
        Error_Handler();
    }
}
void HAL_I2C_MspDeInit(I2C_HandleTypeDef *handle)
{
    if (handle->Instance == I2C1)
    {
        (void)HAL_GPIO_DeInit(GPIOB, GPIO_PIN_8 | GPIO_PIN_9);
    }
}
void HAL_I2S_MspInit(I2S_HandleTypeDef *handle)
{
    if (handle->Instance != I2S0)
    {
        return;
    }
    /* Example routing provides TX only; DIN and MCLK require MSP changes. */
    GPIO_InitTypeDef init = {.Pin = GPIO_PIN_2 | GPIO_PIN_3,
                             .Mode = GPIO_MODE_AF_PP,
                             .Pull = GPIO_NOPULL,
                             .Drive = GPIO_DRIVE_LEVEL1,
                             .Alternate = 5U};
    if (HAL_GPIO_Init(GPIOB, &init) != HAL_OK)
    {
        Error_Handler();
    }
    init.Pin = GPIO_PIN_4;
    init.Alternate = 4U;
    if (HAL_GPIO_Init(GPIOB, &init) != HAL_OK)
    {
        Error_Handler();
    }
}
void HAL_I2S_MspDeInit(I2S_HandleTypeDef *handle)
{
    if (handle->Instance == I2S0)
    {
        (void)HAL_GPIO_DeInit(GPIOB, GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4);
    }
}
