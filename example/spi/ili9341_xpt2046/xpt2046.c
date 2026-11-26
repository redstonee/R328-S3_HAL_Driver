#include "xpt2046.h"
#include "ili9341.h"
#include "display_bus.h"

#define TOUCH_IRQ GPIO_PIN_13 /* PB13, active low; PB12 BUSY is not needed here. */
static uint32_t initialized;
_Static_assert(TOUCH_RAW_X_MAX > TOUCH_RAW_X_MIN && TOUCH_RAW_X_MAX <= 4095U,
               "Invalid X calibration");
_Static_assert(TOUCH_RAW_Y_MAX > TOUCH_RAW_Y_MIN && TOUCH_RAW_Y_MAX <= 4095U,
               "Invalid Y calibration");

static HAL_StatusTypeDef conversion(uint8_t control, uint16_t *value)
{
    uint8_t tx[3] = {control, 0U, 0U}, rx[3];
    HAL_StatusTypeDef status = Display_SPI_TransmitReceive(tx, rx, sizeof(tx), 20U);
    if (status == HAL_OK)
    {
        /* Discard dummy leading bit and three trailing bits; MSB first. */
        *value = (uint16_t)((((uint16_t)rx[1] << 8) | rx[2]) >> 3) & 0x0FFFU;
    }
    return status;
}
static HAL_StatusTypeDef median_axis(uint8_t control, uint16_t *value)
{
    uint16_t samples[5], discarded;
    HAL_StatusTypeDef status = conversion(control, &discarded);
    for (uint32_t i = 0U; i < 5U && status == HAL_OK; ++i)
    {
        status = conversion(control, &samples[i]);
    }
    if (status != HAL_OK)
    {
        return status;
    }
    for (uint32_t i = 1U; i < 5U; ++i)
    {
        uint16_t sample = samples[i];
        uint32_t j = i;
        while (j > 0U && samples[j - 1U] > sample)
        {
            samples[j] = samples[j - 1U];
            --j;
        }
        samples[j] = sample;
    }
    *value = samples[2];
    return HAL_OK;
}
static int bus_ready(void)
{
    return hspi1.Instance == SPI1 && hspi1.State == HAL_SPI_STATE_READY &&
           hspi1.ActualBaudRateHz != 0U && hspi1.Init.CLKPolarity == SPI_POLARITY_LOW &&
           hspi1.Init.CLKPhase == SPI_PHASE_1EDGE && hspi1.Init.NSS == SPI_NSS_SOFT &&
           hspi1.Init.FirstBit == SPI_FIRSTBIT_MSB;
}
HAL_StatusTypeDef XPT2046_Init(void)
{
    initialized = 0U;
    if (!bus_ready())
    {
        return HAL_ERROR;
    }
    static const GPIO_InitTypeDef pin = {
        .Pin = TOUCH_IRQ, .Mode = GPIO_MODE_INPUT, .Pull = GPIO_PULLUP};
    HAL_StatusTypeDef status = HAL_GPIO_Init(GPIOB, &pin);
    if (status != HAL_OK)
    {
        return status;
    }
    status = Display_SPI_Select(BOARD_SPI_TOUCH);
    if (status != HAL_OK)
    {
        return status;
    }
    uint16_t discarded;
    /* 12-bit, differential, PD1:0=00: internal reference off, PENIRQ enabled. */
    status = conversion(0x90U, &discarded);
    HAL_StatusTypeDef release = Display_SPI_Select(BOARD_SPI_NONE);
    if (status == HAL_OK)
    {
        status = release;
    }
    if (status == HAL_OK)
    {
        initialized = 1U;
    }
    return status;
}
HAL_StatusTypeDef XPT2046_ReadRaw(XPT2046_Point *point)
{
    if (point == NULL)
    {
        return HAL_ERROR;
    }
    *point = (XPT2046_Point){0};
    if (initialized == 0U || !bus_ready())
    {
        return HAL_ERROR;
    }
    if (HAL_GPIO_ReadPin(GPIOB, TOUCH_IRQ) == GPIO_PIN_SET)
    {
        return HAL_OK;
    }
    HAL_StatusTypeDef status = Display_SPI_Select(BOARD_SPI_TOUCH);
    if (status != HAL_OK)
    {
        return status;
    }
    /* All commands end with PD=00, including the final conversion, so PENIRQ
     * remains usable while LCD owns SPI. Reads are serialized in foreground. */
    status = median_axis(0xD0U, &point->x);
    if (status == HAL_OK)
    {
        status = median_axis(0x90U, &point->y);
    }
    HAL_StatusTypeDef release = Display_SPI_Select(BOARD_SPI_NONE);
    if (status == HAL_OK)
    {
        status = release;
    }
    if (status == HAL_OK && HAL_GPIO_ReadPin(GPIOB, TOUCH_IRQ) == GPIO_PIN_RESET)
    {
        point->pressed = 1U;
    }
    return status;
}
static uint32_t normalize(uint16_t value, uint32_t low, uint32_t high)
{
    if (value <= low)
    {
        return 0U;
    }
    if (value >= high)
    {
        return 4095U;
    }
    return ((uint32_t)value - low) * 4095U / (high - low);
}
void XPT2046_ToScreen(const XPT2046_Point *raw, uint16_t *x, uint16_t *y)
{
    if (raw == NULL || x == NULL || y == NULL)
    {
        return;
    }
    uint32_t nx = normalize(raw->x, TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX);
    uint32_t ny = normalize(raw->y, TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX);
    if (TOUCH_SWAP_XY != 0U)
    {
        uint32_t temp = nx;
        nx = ny;
        ny = temp;
    }
    if (TOUCH_INVERT_X != 0U)
    {
        nx = 4095U - nx;
    }
    if (TOUCH_INVERT_Y != 0U)
    {
        ny = 4095U - ny;
    }
    *x = (uint16_t)(nx * (ILI9341_WIDTH - 1U) / 4095U);
    *y = (uint16_t)(ny * (ILI9341_HEIGHT - 1U) / 4095U);
}
