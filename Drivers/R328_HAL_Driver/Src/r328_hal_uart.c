#include "r328_hal.h"

static int valid_instance(const UART_TypeDef *instance)
{
    return instance == UART0 || instance == UART1 || instance == UART2 || instance == UART3;
}
static HAL_StatusTypeDef wait_flag(UART_HandleTypeDef *handle, volatile uint32_t *reg,
                                   uint32_t mask, uint32_t start, uint32_t timeout)
{
    UNUSED(handle);
    while ((*reg & mask) == 0U)
    {
        if (timeout != HAL_MAX_DELAY && (uint32_t)(HAL_GetTick() - start) >= timeout)
        {
            return HAL_TIMEOUT;
        }
    }
    return HAL_OK;
}
static HAL_StatusTypeDef finish(UART_HandleTypeDef *handle, HAL_StatusTypeDef status)
{
    handle->State = HAL_UART_STATE_READY;
    HAL_Unlock(&handle->Lock);
    return status;
}
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *handle)
{
    if (handle == NULL || !valid_instance(handle->Instance) || handle->Init.BaudRate == 0U ||
        (handle->Init.WordLength != UART_WORDLENGTH_7B &&
         handle->Init.WordLength != UART_WORDLENGTH_8B) ||
        (handle->Init.StopBits != UART_STOPBITS_1 && handle->Init.StopBits != UART_STOPBITS_2) ||
        (handle->Init.Parity != UART_PARITY_NONE && handle->Init.Parity != UART_PARITY_ODD &&
         handle->Init.Parity != UART_PARITY_EVEN))
    {
        return HAL_ERROR;
    }
    uint32_t pclk = HAL_RCC_GetPCLK2Freq();
    uint64_t denominator = (uint64_t)handle->Init.BaudRate * 16U;
    uint32_t divisor = (uint32_t)(((uint64_t)pclk + denominator / 2U) / denominator);
    if (pclk == 0U || divisor == 0U || divisor > 65535U)
    {
        return HAL_ERROR;
    }
    uint64_t target = denominator * divisor;
    uint64_t error = target > pclk ? target - pclk : pclk - target;
    if (error * 100U > target * 3U)
    {
        return HAL_ERROR;
    }
    if (HAL_TryLock(&handle->Lock) != HAL_OK)
    {
        return HAL_BUSY;
    }
    if (handle->State == HAL_UART_STATE_RESET)
    {
        HAL_UART_MspInit(handle);
    }
    (void)HAL_RCC_UART_EnableClock(handle->Instance);
    (void)HAL_RCC_UART_ForceReset(handle->Instance);
    UART_TypeDef *uart = handle->Instance;
    uint32_t remaining = HAL_REGISTER_POLL_LIMIT;
    while ((uart->USR & 1U) != 0U)
    {
        if (--remaining == 0U)
        {
            handle->State = HAL_UART_STATE_RESET;
            HAL_Unlock(&handle->Lock);
            return HAL_TIMEOUT;
        }
    }
    uart->LCR = 0U;
    uart->IER_DLH = 0U;
    uart->LCR = 0x80U;
    uart->DATA = divisor & 0xFFU;
    uart->IER_DLH = divisor >> 8;
    uart->LCR = handle->Init.WordLength | handle->Init.StopBits | handle->Init.Parity;
    uart->MCR = 0U;
    uart->IIR_FCR = 0x07U; /* IIR is read-only at this address: never read/modify/write FCR. */
    R328_DSB();
    handle->ErrorCode = HAL_UART_ERROR_NONE;
    return finish(handle, HAL_OK);
}
HAL_StatusTypeDef HAL_UART_DeInit(UART_HandleTypeDef *handle)
{
    if (handle == NULL || !valid_instance(handle->Instance))
    {
        return HAL_ERROR;
    }
    if (HAL_TryLock(&handle->Lock) != HAL_OK)
    {
        return HAL_BUSY;
    }
    (void)HAL_RCC_UART_ForceReset(handle->Instance);
    HAL_UART_MspDeInit(handle);
    handle->State = HAL_UART_STATE_RESET;
    handle->ErrorCode = HAL_UART_ERROR_NONE;
    HAL_Unlock(&handle->Lock);
    return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *handle, const uint8_t *data, uint32_t size,
                                    uint32_t timeout)
{
    if (handle == NULL || !valid_instance(handle->Instance) || (data == NULL && size != 0U) ||
        handle->State == HAL_UART_STATE_RESET)
    {
        return HAL_ERROR;
    }
    if (HAL_TryLock(&handle->Lock) != HAL_OK)
    {
        return HAL_BUSY;
    }
    handle->State = HAL_UART_STATE_BUSY_TX;
    uint32_t start = HAL_GetTick();
    for (uint32_t index = 0U; index < size; ++index)
    {
        HAL_StatusTypeDef status = wait_flag(handle, &handle->Instance->USR, 2U, start, timeout);
        if (status != HAL_OK)
        {
            return finish(handle, status);
        }
        handle->Instance->DATA = data[index];
    }
    HAL_StatusTypeDef status =
        size == 0U ? HAL_OK : wait_flag(handle, &handle->Instance->LSR, 1U << 6, start, timeout);
    return finish(handle, status);
}
HAL_StatusTypeDef HAL_UART_Receive(UART_HandleTypeDef *handle, uint8_t *data, uint32_t size,
                                   uint32_t timeout)
{
    if (handle == NULL || !valid_instance(handle->Instance) || (data == NULL && size != 0U) ||
        handle->State == HAL_UART_STATE_RESET)
    {
        return HAL_ERROR;
    }
    if (HAL_TryLock(&handle->Lock) != HAL_OK)
    {
        return HAL_BUSY;
    }
    handle->State = HAL_UART_STATE_BUSY_RX;
    handle->ErrorCode = HAL_UART_ERROR_NONE;
    uint32_t start = HAL_GetTick();
    for (uint32_t index = 0U; index < size; ++index)
    {
        for (;;)
        {
            uint32_t lsr = handle->Instance->LSR;
            if ((lsr & 0x1EU) != 0U)
            {
                handle->ErrorCode = lsr & 0x1EU;
                if ((lsr & 1U) != 0U)
                {
                    (void)handle->Instance->DATA;
                }
                return finish(handle, HAL_ERROR);
            }
            if ((lsr & 1U) != 0U)
            {
                break;
            }
            if (timeout != HAL_MAX_DELAY && (uint32_t)(HAL_GetTick() - start) >= timeout)
            {
                return finish(handle, HAL_TIMEOUT);
            }
        }
        data[index] = (uint8_t)handle->Instance->DATA;
    }
    return finish(handle, HAL_OK);
}
__weak void HAL_UART_MspInit(UART_HandleTypeDef *handle)
{
    UNUSED(handle);
}
__weak void HAL_UART_MspDeInit(UART_HandleTypeDef *handle)
{
    UNUSED(handle);
}
