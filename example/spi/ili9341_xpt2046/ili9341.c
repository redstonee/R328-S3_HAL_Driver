#include "ili9341.h"
#include "display_bus.h"

/* Example wiring: PH2 reset, PH3 D/C, PH4 CS, PH9 active-high backlight.
 * LCD commands are write-only; XPT2046 uses the shared MISO line.
 * Register meanings: ILI9341 datasheet V1.11 sections 8 and 15.4. */
#define LCD_RST GPIO_PIN_2
#define LCD_DC GPIO_PIN_3
#define LCD_BL GPIO_PIN_9
#define LCD_TIMEOUT 100U
static uint32_t initialized;

void ILI9341_Backlight(uint32_t enabled)
{
    HAL_GPIO_WritePin(GPIOH, LCD_BL, enabled != 0U ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
static HAL_StatusTypeDef deselect(HAL_StatusTypeDef status)
{
    HAL_StatusTypeDef release = Display_SPI_Select(DISPLAY_SPI_NONE);
    return status != HAL_OK ? status : release;
}
/* Caller owns LCD CS; D/C may change only between completed byte transfers. */
static HAL_StatusTypeDef command(uint8_t code, const uint8_t *data, uint32_t size)
{
    HAL_GPIO_WritePin(GPIOH, LCD_DC, GPIO_PIN_RESET);
    HAL_StatusTypeDef status = Display_SPI_Transmit(&code, 1U, LCD_TIMEOUT);
    HAL_GPIO_WritePin(GPIOH, LCD_DC, GPIO_PIN_SET);
    if (status == HAL_OK && size != 0U)
    {
        status = Display_SPI_Transmit(data, size, LCD_TIMEOUT);
    }
    return status;
}
HAL_StatusTypeDef ILI9341_Init(void)
{
    initialized = 0U;
    /* Preload before changing mux; keep the backlight off until GRAM is cleared. */
    HAL_GPIO_WritePin(GPIOH, LCD_RST | LCD_DC, GPIO_PIN_SET);
    ILI9341_Backlight(0U);
    static const GPIO_InitTypeDef pins = {.Pin = LCD_RST | LCD_DC | LCD_BL,
                                          .Mode = GPIO_MODE_OUTPUT_PP,
                                          .Pull = GPIO_NOPULL,
                                          .Drive = GPIO_DRIVE_LEVEL1};
    HAL_StatusTypeDef status = HAL_GPIO_Init(GPIOH, &pins);
    if (status != HAL_OK)
    {
        return status;
    }
    status = hspi1.State == HAL_SPI_STATE_READY ? HAL_OK : HAL_ERROR;
    if (status != HAL_OK)
    {
        return status;
    }
    HAL_GPIO_WritePin(GPIOH, LCD_RST, GPIO_PIN_RESET);
    HAL_Delay(20U);
    HAL_GPIO_WritePin(GPIOH, LCD_RST, GPIO_PIN_SET);
    HAL_Delay(150U); /* Allow recovery even if reset was asserted in Sleep Out. */

    /* Documented power, scan and interface registers for a typical 240x320 panel.
     * Panel-specific VCOM/gamma tuning may differ. Fixed portrait MX+BGR scan. */
    static const struct
    {
        uint8_t code, size, data[3];
    } setup[] = {
        {0x28U, 0U, {0U}},                  /* Display off */
        {0xC0U, 1U, {0x23U}},               /* Power control 1 */
        {0xC1U, 1U, {0x10U}},               /* Power control 2 */
        {0xC5U, 2U, {0x3EU, 0x28U}},        /* VCOM control 1 */
        {0xC7U, 1U, {0x86U}},               /* VCOM control 2 */
        {0x36U, 1U, {0x48U}},               /* MADCTL: MX | BGR */
        {0x3AU, 1U, {0x55U}},               /* RGB565, 16 bits/pixel */
        {0xB1U, 2U, {0x00U, 0x18U}},        /* Frame rate */
        {0xB6U, 3U, {0x08U, 0x82U, 0x27U}}, /* Display function */
        {0x26U, 1U, {0x01U}},               /* Gamma curve 1 */
        {0x20U, 0U, {0U}},                  /* Inversion off */
        {0x13U, 0U, {0U}},                  /* Normal mode */
        {0x11U, 0U, {0U}},                  /* Sleep out */
    };
    status = Display_SPI_Select(DISPLAY_SPI_LCD);
    if (status != HAL_OK)
    {
        return status;
    }
    for (uint32_t i = 0U; i < sizeof(setup) / sizeof(setup[0]) && status == HAL_OK; ++i)
    {
        status = command(setup[i].code, setup[i].data, setup[i].size);
    }
    status = deselect(status);
    if (status != HAL_OK)
    {
        return status;
    }
    HAL_Delay(150U);
    status = Display_SPI_Select(DISPLAY_SPI_LCD);
    if (status != HAL_OK)
    {
        return status;
    }
    status = deselect(command(0x29U, NULL, 0U)); /* Display on */
    if (status != HAL_OK)
    {
        return status;
    }
    HAL_Delay(20U);
    initialized = 1U;
    status = ILI9341_Fill(ILI9341_BLACK);
    if (status != HAL_OK)
    {
        initialized = 0U;
    }
    else
    {
        ILI9341_Backlight(1U);
    }
    return status;
}
HAL_StatusTypeDef ILI9341_FillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                                   uint16_t rgb565)
{
    if (initialized == 0U || hspi1.State != HAL_SPI_STATE_READY)
    {
        return HAL_ERROR;
    }
    if (width == 0U || height == 0U || x >= ILI9341_WIDTH || y >= ILI9341_HEIGHT)
    {
        return HAL_OK;
    }
    /* Clip using subtraction so x+width and y+height cannot overflow. */
    if (width > ILI9341_WIDTH - x)
    {
        width = (uint16_t)(ILI9341_WIDTH - x);
    }
    if (height > ILI9341_HEIGHT - y)
    {
        height = (uint16_t)(ILI9341_HEIGHT - y);
    }
    uint16_t x_end = (uint16_t)(x + width - 1U), y_end = (uint16_t)(y + height - 1U);
    uint8_t columns[] = {(uint8_t)(x >> 8), (uint8_t)x, (uint8_t)(x_end >> 8), (uint8_t)x_end};
    uint8_t rows[] = {(uint8_t)(y >> 8), (uint8_t)y, (uint8_t)(y_end >> 8), (uint8_t)y_end};
    /* Cross FIFO boundaries in all modes without allocating a framebuffer. */
    uint8_t pixels[ILI9341_PIXEL_BUFFER_SIZE];
    for (uint32_t i = 0U; i < sizeof(pixels); i += 2U)
    {
        pixels[i] = (uint8_t)(rgb565 >> 8);
        pixels[i + 1U] = (uint8_t)rgb565;
    }
    HAL_StatusTypeDef status = Display_SPI_Select(DISPLAY_SPI_LCD);
    if (status != HAL_OK)
    {
        return status;
    }
    status = command(0x2AU, columns, sizeof(columns));
    if (status == HAL_OK)
    {
        status = command(0x2BU, rows, sizeof(rows));
    }
    if (status == HAL_OK)
    {
        status = command(0x2CU, NULL, 0U);
    }
    uint32_t remaining = (uint32_t)width * height * 2U;
    while (remaining != 0U && status == HAL_OK)
    {
        uint32_t count = remaining > sizeof(pixels) ? sizeof(pixels) : remaining;
        status = Display_SPI_Transmit(pixels, count, LCD_TIMEOUT);
        remaining -= count;
    }
    /* Every error path releases LCD CS before touch or another device can run. */
    return deselect(status);
}
HAL_StatusTypeDef ILI9341_Fill(uint16_t rgb565)
{
    return ILI9341_FillRect(0U, 0U, ILI9341_WIDTH, ILI9341_HEIGHT, rgb565);
}
