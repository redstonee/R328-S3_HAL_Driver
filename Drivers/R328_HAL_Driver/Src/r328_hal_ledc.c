#include "r328_hal_bus_private.h"

#define LEDC_ENABLE 1U
#define LEDC_SOFT_RESET (1U << 1)
#define LEDC_MSB_FIRST 0x3CU /* GRB bypass, all channels and combined word MSB first. */
#define LEDC_RESET_LED (1U << 10)
#define LEDC_FINISHED 1U
#define LEDC_OVERFLOW (1U << 4)
#define LEDC_UNDERRUN (1U << 3)
#define LEDC_W1C 0x1BU
#define LEDC_FIFO_DEPTH 32U

static HAL_StatusTypeDef finish(LEDC_HandleTypeDef *handle, HAL_StatusTypeDef status)
{
    if (status == HAL_OK)
    {
        BUS_WRITE(handle->Instance->INT_STS, LEDC_W1C);
        handle->State = HAL_LEDC_STATE_READY;
    }
    else
    {
        /* CCU reset also stops a wedged state machine. Do not silently resume
         * after starvation: the LEDs may have latched an incomplete frame. */
        (void)HAL_RCC_LEDC_ForceReset(handle->Instance);
        handle->State = HAL_LEDC_STATE_RESET;
    }
    HAL_Unlock(&handle->Lock);
    return status;
}

HAL_StatusTypeDef HAL_LEDC_Init(LEDC_HandleTypeDef *handle)
{
    if (handle == NULL || handle->Instance != LEDC)
    {
        return HAL_ERROR;
    }
    const LEDC_InitTypeDef *init = &handle->Init;
    if (init->LEDCount == 0U || init->LEDCount > LEDC_MAX_LEDS || init->T0H < 2U ||
        init->T0H > 32U || init->T0L < 2U || init->T0L > 64U || init->T1H < 2U || init->T1H > 64U ||
        init->T1L < 2U || init->T1L > 32U || init->ResetTimeUs == 0U || init->ResetTimeUs > 341U ||
        init->WaitDataTimeUs == 0U || init->WaitDataTimeUs > 341U)
    {
        return HAL_ERROR;
    }
    if (HAL_TryLock(&handle->Lock) != HAL_OK)
    {
        return HAL_BUSY;
    }
    handle->ErrorCode = HAL_LEDC_ERROR_NONE;
    (void)HAL_RCC_LEDC_EnableClock(handle->Instance);
    (void)HAL_RCC_LEDC_ForceReset(handle->Instance);
    LEDC_TypeDef *ledc = handle->Instance;
    BUS_WRITE(ledc->CTRL, LEDC_MSB_FIRST | LEDC_SOFT_RESET);
    if (bus_wait_clear(&ledc->CTRL, LEDC_SOFT_RESET) != HAL_OK)
    {
        handle->ErrorCode = HAL_LEDC_ERROR_RESET;
        return finish(handle, HAL_TIMEOUT);
    }
    BUS_WRITE(ledc->TIMING, ((init->T1H - 1U) << 21) | ((init->T1L - 1U) << 16) |
                                ((init->T0H - 1U) << 6) | (init->T0L - 1U));
    BUS_WRITE(ledc->FINISH, (init->WaitDataTimeUs * 24U - 1U) << 16);
    BUS_WRITE(ledc->RESET_TIMING, ((init->ResetTimeUs * 24U - 1U) << 16) | (init->LEDCount - 1U));
    BUS_WRITE(ledc->WAIT_TIME0, 0U);
    BUS_WRITE(ledc->WAIT_TIME1, 0U);
    BUS_WRITE(ledc->DMA_CTRL, 0U); /* PIO; no DMA requests. */
    BUS_WRITE(ledc->INT_CTRL, 0U); /* Poll status without enabling IRQs. */
    if (handle->State == HAL_LEDC_STATE_RESET)
    {
        HAL_LEDC_MspInit(handle);
    }
    /* Start with the external chain in its frame-boundary state. */
    BUS_WRITE(ledc->CTRL, LEDC_MSB_FIRST | LEDC_RESET_LED);
    if (bus_wait_clear(&ledc->CTRL, LEDC_RESET_LED) != HAL_OK)
    {
        handle->ErrorCode = HAL_LEDC_ERROR_RESET;
        return finish(handle, HAL_TIMEOUT);
    }
    return finish(handle, HAL_OK);
}

HAL_StatusTypeDef HAL_LEDC_DeInit(LEDC_HandleTypeDef *handle)
{
    if (handle == NULL || handle->Instance != LEDC)
    {
        return HAL_ERROR;
    }
    if (HAL_TryLock(&handle->Lock) != HAL_OK)
    {
        return HAL_BUSY;
    }
    (void)HAL_RCC_LEDC_ForceReset(handle->Instance);
    HAL_LEDC_MspDeInit(handle);
    handle->State = HAL_LEDC_STATE_RESET;
    HAL_Unlock(&handle->Lock);
    return HAL_OK;
}

HAL_StatusTypeDef HAL_LEDC_Transmit(LEDC_HandleTypeDef *handle, const uint32_t *colors,
                                    uint32_t count, uint32_t timeout)
{
    if (handle == NULL || handle->Instance != LEDC || colors == NULL || count == 0U ||
        count > LEDC_MAX_LEDS || count != handle->Init.LEDCount ||
        handle->State == HAL_LEDC_STATE_RESET)
    {
        return HAL_ERROR;
    }
    if (HAL_TryLock(&handle->Lock) != HAL_OK)
    {
        return HAL_BUSY;
    }
    handle->State = HAL_LEDC_STATE_BUSY;
    handle->ErrorCode = HAL_LEDC_ERROR_NONE;
    LEDC_TypeDef *ledc = handle->Instance;
    uint32_t start = HAL_GetTick();
    BUS_WRITE(ledc->CTRL, LEDC_MSB_FIRST | LEDC_SOFT_RESET);
    if (bus_wait_clear(&ledc->CTRL, LEDC_SOFT_RESET) != HAL_OK)
    {
        handle->ErrorCode = HAL_LEDC_ERROR_RESET;
        return finish(handle, HAL_TIMEOUT);
    }
    BUS_WRITE(ledc->INT_STS, LEDC_W1C);
    uint32_t sent = count < LEDC_FIFO_DEPTH ? count : LEDC_FIFO_DEPTH;
    for (uint32_t i = 0U; i < sent; ++i)
    {
        BUS_WRITE(ledc->DATA, colors[i] & 0xFFFFFFU);
    }
    R328_DSB();
    BUS_WRITE(ledc->CTRL, LEDC_MSB_FIRST | (count << 16) | LEDC_ENABLE);
    for (;;)
    {
        uint32_t status = BUS_READ(ledc->INT_STS);
        if ((status & (LEDC_OVERFLOW | LEDC_UNDERRUN)) != 0U)
        {
            handle->ErrorCode = ((status & LEDC_OVERFLOW) != 0U ? HAL_LEDC_ERROR_FIFO : 0U) |
                                ((status & LEDC_UNDERRUN) != 0U ? HAL_LEDC_ERROR_UNDERRUN : 0U);
            return finish(handle, HAL_ERROR);
        }
        if ((status & LEDC_FINISHED) != 0U && (BUS_READ(ledc->CTRL) & LEDC_ENABLE) == 0U)
        {
            break;
        }
        uint32_t used = (status >> 10) & 0x3FU;
        if (used > LEDC_FIFO_DEPTH)
        {
            handle->ErrorCode = HAL_LEDC_ERROR_FIFO;
            return finish(handle, HAL_ERROR);
        }
        uint32_t free_words = LEDC_FIFO_DEPTH - used;
        while (sent < count && free_words != 0U)
        {
            BUS_WRITE(ledc->DATA, colors[sent++] & 0xFFFFFFU);
            --free_words;
        }
        if (bus_expired(start, timeout))
        {
            handle->ErrorCode = HAL_LEDC_ERROR_TIMEOUT;
            return finish(handle, HAL_TIMEOUT);
        }
    }
    /* Explicitly wait for the final low latch pulse, even if TRANS_FINISH
     * becomes visible before the hardware's automatic inter-frame reset. */
    BUS_WRITE(ledc->CTRL, LEDC_MSB_FIRST | LEDC_RESET_LED);
    while ((BUS_READ(ledc->CTRL) & LEDC_RESET_LED) != 0U)
    {
        if (bus_expired(start, timeout))
        {
            handle->ErrorCode = HAL_LEDC_ERROR_TIMEOUT;
            return finish(handle, HAL_TIMEOUT);
        }
    }
    return finish(handle, HAL_OK);
}

__weak void HAL_LEDC_MspInit(LEDC_HandleTypeDef *handle)
{
    UNUSED(handle);
}
__weak void HAL_LEDC_MspDeInit(LEDC_HandleTypeDef *handle)
{
    UNUSED(handle);
}
