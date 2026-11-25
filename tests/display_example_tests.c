/* Protocol model for the real LCD/touch example drivers. The HAL register/FIFO
 * implementation has separate bus tests; this checks the shared-device traffic. */
#include "ili9341.h"
#include "xpt2046.h"
#include <assert.h>
#include <stdio.h>

SPI_HandleTypeDef hspi1;
static uint32_t gpio_h = UINT32_MAX;
static uint32_t pen_down;
static uint32_t tx_calls, fail_tx, touch_calls, fail_touch;
static uint32_t lcd_pixel_bytes, window_remaining;
static uint16_t column_start, column_end, row_start, row_end, pixel_color;
static uint8_t opcode;
static uint32_t new_pixels;
static uint32_t x_samples, y_samples;

static Board_SPI_Device selected(void)
{
    uint32_t lcd = (gpio_h & GPIO_PIN_4) == 0U;
    uint32_t touch = (gpio_h & GPIO_PIN_8) == 0U;
    assert(!(lcd && touch));
    return lcd != 0U ? BOARD_SPI_LCD : touch != 0U ? BOARD_SPI_TOUCH : BOARD_SPI_NONE;
}
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint32_t pins, GPIO_PinState state)
{
    assert(port == GPIOH);
    assert((pins & (GPIO_PIN_0 | GPIO_PIN_1)) == 0U);
    if (state == GPIO_PIN_SET)
    {
        gpio_h |= pins;
    }
    else
    {
        gpio_h &= ~pins;
    }
    (void)selected();
    if ((pins & GPIO_PIN_2) != 0U && state == GPIO_PIN_RESET)
    {
        window_remaining = 0U;
        opcode = 0U;
    }
}
HAL_StatusTypeDef HAL_GPIO_Init(GPIO_TypeDef *port, const GPIO_InitTypeDef *pins)
{
    assert((port == GPIOH && pins->Pin == (GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_9)) ||
           (port == GPIOB && pins->Pin == GPIO_PIN_13 && pins->Mode == GPIO_MODE_INPUT));
    return HAL_OK;
}
GPIO_PinState HAL_GPIO_ReadPin(const GPIO_TypeDef *port, uint32_t pin)
{
    assert(port == GPIOB && pin == GPIO_PIN_13);
    return pen_down != 0U ? GPIO_PIN_RESET : GPIO_PIN_SET;
}
void HAL_Delay(uint32_t ms)
{
    (void)ms;
}
HAL_StatusTypeDef Board_SPI1_Init(uint32_t hz)
{
    assert(hz <= 2000000U);
    hspi1.Instance = SPI1;
    hspi1.State = HAL_SPI_STATE_READY;
    hspi1.ActualBaudRateHz = hz;
    hspi1.Init = (SPI_InitTypeDef){.CLKPolarity = SPI_POLARITY_LOW,
                                   .CLKPhase = SPI_PHASE_1EDGE,
                                   .NSS = SPI_NSS_SOFT,
                                   .FirstBit = SPI_FIRSTBIT_MSB};
    return Board_SPI1_Select(BOARD_SPI_NONE);
}
HAL_StatusTypeDef Board_SPI1_Select(Board_SPI_Device device)
{
    assert(device == BOARD_SPI_NONE || hspi1.State == HAL_SPI_STATE_READY);
    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_4 | GPIO_PIN_8, GPIO_PIN_SET);
    if (device != BOARD_SPI_NONE)
    {
        HAL_GPIO_WritePin(GPIOH, device == BOARD_SPI_LCD ? GPIO_PIN_4 : GPIO_PIN_8, GPIO_PIN_RESET);
    }
    return HAL_OK;
}
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *handle, const uint8_t *data, uint32_t size,
                                   uint32_t timeout)
{
    assert(handle == &hspi1 && selected() == BOARD_SPI_LCD && timeout != 0U);
    ++tx_calls;
    if (tx_calls == fail_tx)
    {
        hspi1.State = HAL_SPI_STATE_RESET;
        return HAL_TIMEOUT;
    }
    if ((gpio_h & GPIO_PIN_3) == 0U)
    {
        assert(size == 1U);
        opcode = data[0];
        if (opcode == 0x2CU)
        {
            assert(window_remaining == 0U && column_end >= column_start && row_end >= row_start);
            window_remaining = ((uint32_t)column_end - column_start + 1U) *
                               ((uint32_t)row_end - row_start + 1U) * 2U;
            new_pixels = 1U;
        }
    }
    else if (opcode == 0x2AU || opcode == 0x2BU)
    {
        assert(size == 4U);
        uint16_t first = (uint16_t)((uint16_t)data[0] << 8) | data[1];
        uint16_t last = (uint16_t)((uint16_t)data[2] << 8) | data[3];
        if (opcode == 0x2AU)
        {
            column_start = first;
            column_end = last;
            assert(last < 240U);
        }
        else
        {
            row_start = first;
            row_end = last;
            assert(last < 320U);
        }
    }
    else if (opcode == 0x2CU)
    {
        assert(size <= window_remaining && (size & 1U) == 0U && size <= 64U);
        for (uint32_t i = 0U; i < size; i += 2U)
        {
            uint16_t color = (uint16_t)((uint16_t)data[i] << 8) | data[i + 1U];
            if (new_pixels != 0U)
            {
                pixel_color = color;
                new_pixels = 0U;
            }
            assert(color == pixel_color);
        }
        lcd_pixel_bytes += size;
        window_remaining -= size;
    }
    else if (opcode == 0x3AU)
    {
        assert(size == 1U && data[0] == 0x55U); /* RGB565 */
    }
    else if (opcode == 0x36U)
    {
        assert(size == 1U && data[0] == 0x48U); /* Fixed portrait scan */
    }
    return HAL_OK;
}
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *handle, const uint8_t *tx, uint8_t *rx,
                                          uint32_t size, uint32_t timeout)
{
    assert(handle == &hspi1 && selected() == BOARD_SPI_TOUCH && size == 3U && timeout != 0U);
    assert(tx[0] == 0xD0U || tx[0] == 0x90U); /* PD=00, 12-bit differential. */
    assert(tx[1] == 0U && tx[2] == 0U);
    ++touch_calls;
    if (touch_calls == fail_touch)
    {
        hspi1.State = HAL_SPI_STATE_RESET;
        return HAL_TIMEOUT;
    }
    /* First sample is stale. Two extreme outliers must not move the median. */
    static const uint16_t x[] = {3000U, 1000U, 1002U, 4095U, 1001U, 2U};
    static const uint16_t y[] = {2000U, 3000U, 2999U, 0U, 3001U, 4095U};
    uint16_t value = tx[0] == 0xD0U ? x[x_samples++ % 6U] : y[y_samples++ % 6U];
    uint16_t serial = (uint16_t)((value << 3) | 7U);
    rx[0] = 0xFFU;
    rx[1] = (uint8_t)(serial >> 8);
    rx[2] = (uint8_t)serial;
    return HAL_OK;
}
int main(void)
{
    XPT2046_Point point;
    assert(ILI9341_Fill(ILI9341_RED) == HAL_ERROR);
    assert(XPT2046_ReadRaw(&point) == HAL_ERROR && point.pressed == 0U);
    assert(ILI9341_Init() == HAL_OK);
    assert(lcd_pixel_bytes == 240U * 320U * 2U && pixel_color == ILI9341_BLACK);
    assert((gpio_h & GPIO_PIN_9) != 0U && selected() == BOARD_SPI_NONE);
    assert(XPT2046_Init() == HAL_OK && selected() == BOARD_SPI_NONE);

    uint32_t before = tx_calls;
    assert(ILI9341_FillRect(240U, 0U, 5U, 5U, ILI9341_RED) == HAL_OK);
    assert(ILI9341_FillRect(0U, 0U, 0U, 5U, ILI9341_RED) == HAL_OK);
    assert(tx_calls == before);
    uint32_t bytes_before = lcd_pixel_bytes;
    assert(ILI9341_FillRect(238U, 318U, UINT16_MAX, UINT16_MAX, 0x1234U) == HAL_OK);
    assert(lcd_pixel_bytes - bytes_before == 8U && pixel_color == 0x1234U);
    assert(column_start == 238U && column_end == 239U && row_start == 318U && row_end == 319U);
    assert(selected() == BOARD_SPI_NONE);

    before = touch_calls;
    assert(XPT2046_ReadRaw(&point) == HAL_OK && point.pressed == 0U);
    assert(touch_calls == before); /* No SPI traffic with PENIRQ inactive. */
    pen_down = 1U;
    x_samples = 0U;
    y_samples = 0U;
    assert(XPT2046_ReadRaw(&point) == HAL_OK && point.pressed != 0U);
    assert(point.x == 1001U && point.y == 3000U && selected() == BOARD_SPI_NONE);
    assert(ILI9341_FillRect(10U, 10U, 5U, 5U, ILI9341_WHITE) == HAL_OK);
    assert(selected() == BOARD_SPI_NONE); /* Switch back from touch to LCD. */
    uint16_t x, y;
    point.x = 0U;
    point.y = 4095U;
    XPT2046_ToScreen(&point, &x, &y);
    assert(x == 239U && y == 0U); /* Clamping, default axis swap. */
    point.x = 4095U;
    point.y = 4095U;
    XPT2046_ToScreen(&point, &x, &y);
    assert(x == 239U && y == 319U);

    /* A FIFO/timeout failure must release CS and propagate the original error. */
    fail_touch = touch_calls + 3U;
    assert(XPT2046_ReadRaw(&point) == HAL_TIMEOUT && point.pressed == 0U);
    assert(selected() == BOARD_SPI_NONE);
    fail_touch = 0U;
    assert(ILI9341_Init() == HAL_OK && XPT2046_Init() == HAL_OK);
    fail_tx = tx_calls + 6U; /* First pixel burst, after CASET/PASET/RAMWR. */
    assert(ILI9341_Fill(ILI9341_BLUE) == HAL_TIMEOUT && selected() == BOARD_SPI_NONE);
    fail_tx = tx_calls + 1U;
    assert(ILI9341_Init() == HAL_TIMEOUT);
    assert(selected() == BOARD_SPI_NONE && (gpio_h & GPIO_PIN_9) == 0U);
    puts("LCD/touch shared SPI protocol checks passed");
    return 0;
}
