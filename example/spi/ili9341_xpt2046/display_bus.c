#include "display_bus.h"
#include "ili9341.h"
#include "xpt2046.h"

SPI_HandleTypeDef hspi1;

static HAL_StatusTypeDef select_pins(Display_SPI_Device device)
{
    if (device != DISPLAY_SPI_NONE && device != DISPLAY_SPI_LCD && device != DISPLAY_SPI_TOUCH)
    {
        return HAL_ERROR;
    }
    if (hspi1.State == HAL_SPI_STATE_RESET && device != DISPLAY_SPI_NONE)
    {
        return HAL_ERROR;
    }
    if (HAL_TryLock(&hspi1.Lock) != HAL_OK)
    {
        return HAL_BUSY;
    }
    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_4 | GPIO_PIN_8, GPIO_PIN_SET);
    if (device != DISPLAY_SPI_NONE)
    {
        HAL_GPIO_WritePin(GPIOH, device == DISPLAY_SPI_LCD ? GPIO_PIN_4 : GPIO_PIN_8,
                          GPIO_PIN_RESET);
    }
    HAL_Unlock(&hspi1.Lock);
    return HAL_OK;
}
#define DISPLAY_TX_DMA_CHANNEL 0U
#define DISPLAY_RX_DMA_CHANNEL 1U
static DMA_HandleTypeDef tx_dma = {
    .Init = {DISPLAY_TX_DMA_CHANNEL, DMA_MEMORY_TO_PERIPH, DMA_REQUEST_SPI1}};
static DMA_HandleTypeDef rx_dma = {
    .Init = {DISPLAY_RX_DMA_CHANNEL, DMA_PERIPH_TO_MEMORY, DMA_REQUEST_SPI1}};
static Display_SPI_Mode transfer_mode = DISPLAY_SPI_POLLING;
static volatile uint32_t completed, failed;

HAL_StatusTypeDef Display_SPI_Select(Display_SPI_Device device)
{
    if (device == DISPLAY_SPI_NONE)
    {
        return select_pins(device);
    }
    if (device != DISPLAY_SPI_LCD && device != DISPLAY_SPI_TOUCH)
    {
        return HAL_ERROR;
    }
    if (hspi1.State != HAL_SPI_STATE_READY)
    {
        return hspi1.State == HAL_SPI_STATE_BUSY ? HAL_BUSY : HAL_ERROR;
    }
    HAL_StatusTypeDef result = select_pins(DISPLAY_SPI_NONE);
    uint32_t hz = device == DISPLAY_SPI_LCD ? ILI9341_SPI_HZ : XPT2046_SPI_HZ;
    if (result == HAL_OK && hspi1.Init.BaudRateHz != hz)
    {
        result = HAL_SPI_SetBaudRate(&hspi1, hz);
    }
    return result == HAL_OK ? select_pins(device) : result;
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *h)
{
    if (h == &hspi1)
    {
        completed = 1U;
    }
}
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *h)
{
    if (h == &hspi1)
    {
        completed = 1U;
    }
}
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *h)
{
    if (h == &hspi1)
    {
        failed = 1U;
    }
}
HAL_StatusTypeDef Display_SPI_Init(void)
{
    if (hspi1.State == HAL_SPI_STATE_BUSY)
    {
        return HAL_BUSY;
    }
    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_4 | GPIO_PIN_8, GPIO_PIN_SET);
    GPIO_InitTypeDef pins = {.Pin = GPIO_PIN_4 | GPIO_PIN_8,
                             .Mode = GPIO_MODE_OUTPUT_PP,
                             .Pull = GPIO_PULLUP,
                             .Drive = GPIO_DRIVE_LEVEL1};
    HAL_StatusTypeDef result = HAL_GPIO_Init(GPIOH, &pins);
    if (result != HAL_OK)
    {
        return result;
    }
    pins.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    pins.Mode = GPIO_MODE_AF_PP;
    pins.Pull = GPIO_NOPULL;
    pins.Alternate = 3U;
    result = HAL_GPIO_Init(GPIOH, &pins);
    if (result != HAL_OK)
    {
        return result;
    }
    hspi1.Instance = SPI1;
    hspi1.Init = (SPI_InitTypeDef){.Mode = SPI_MODE_MASTER,
                                   .DataSize = SPI_DATASIZE_8BIT,
                                   .CLKPolarity = SPI_POLARITY_LOW,
                                   .CLKPhase = SPI_PHASE_1EDGE,
                                   .NSS = SPI_NSS_SOFT,
                                   .BaudRateHz = ILI9341_SPI_HZ,
                                   .FirstBit = SPI_FIRSTBIT_MSB,
                                   .ChipSelect = 0U};
    result = HAL_SPI_Init(&hspi1);
    if (result != HAL_OK)
    {
        return result;
    }
    result = HAL_DMA_Init(&tx_dma);
    if (result == HAL_OK)
    {
        result = HAL_DMA_Init(&rx_dma);
    }
    if (result == HAL_OK)
    {
        hspi1.hdmatx = &tx_dma;
        hspi1.hdmarx = &rx_dma;
        transfer_mode = DISPLAY_SPI_POLLING;
    }
    return result;
}
HAL_StatusTypeDef Display_SPI_SetMode(Display_SPI_Mode mode)
{
    if (mode != DISPLAY_SPI_POLLING && mode != DISPLAY_SPI_INTERRUPT && mode != DISPLAY_SPI_DMA)
    {
        return HAL_ERROR;
    }
    if (hspi1.State != HAL_SPI_STATE_READY)
    {
        return HAL_BUSY;
    }
    HAL_StatusTypeDef result = select_pins(DISPLAY_SPI_NONE);
    if (result == HAL_OK)
    {
        transfer_mode = mode;
    }
    return result;
}
static HAL_StatusTypeDef wait_complete(HAL_StatusTypeDef result)
{
    if (result != HAL_OK)
    {
        /* A failed start can still retain a DMA buffer if abort timed out.
         * Keep local device buffers alive until cleanup finishes. */
        while (hspi1.State == HAL_SPI_STATE_BUSY)
        {
            HAL_SPI_Process(&hspi1);
        }
        return result;
    }
    while (completed == 0U && failed == 0U)
    {
        HAL_SPI_Process(&hspi1);
    }
    return failed == 0U                                      ? HAL_OK
           : (hspi1.ErrorCode & HAL_SPI_ERROR_TIMEOUT) != 0U ? HAL_TIMEOUT
                                                             : HAL_ERROR;
}
HAL_StatusTypeDef Display_SPI_Transmit(const uint8_t *data, uint32_t size, uint32_t timeout)
{
    if (transfer_mode == DISPLAY_SPI_POLLING)
    {
        return HAL_SPI_Transmit(&hspi1, data, size, timeout);
    }
    HAL_StatusTypeDef result = HAL_SPI_SetAsyncTimeout(&hspi1, timeout);
    if (result != HAL_OK)
    {
        return result;
    }
    completed = failed = 0U;
    return wait_complete(transfer_mode == DISPLAY_SPI_INTERRUPT
                             ? HAL_SPI_Transmit_IT(&hspi1, data, size)
                             : HAL_SPI_Transmit_DMA(&hspi1, data, size));
}
HAL_StatusTypeDef Display_SPI_TransmitReceive(const uint8_t *tx, uint8_t *rx, uint32_t size,
                                              uint32_t timeout)
{
    if (transfer_mode == DISPLAY_SPI_POLLING)
    {
        return HAL_SPI_TransmitReceive(&hspi1, tx, rx, size, timeout);
    }
    HAL_StatusTypeDef result = HAL_SPI_SetAsyncTimeout(&hspi1, timeout);
    if (result != HAL_OK)
    {
        return result;
    }
    completed = failed = 0U;
    return wait_complete(transfer_mode == DISPLAY_SPI_INTERRUPT
                             ? HAL_SPI_TransmitReceive_IT(&hspi1, tx, rx, size)
                             : HAL_SPI_TransmitReceive_DMA(&hspi1, tx, rx, size));
}
