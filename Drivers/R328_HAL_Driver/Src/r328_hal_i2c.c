#include "r328_hal_bus_private.h"
#define TWI_BUS_EN (1U << 6)
#define TWI_START (1U << 5)
#define TWI_STOP (1U << 4)
#define TWI_IF (1U << 3)
#define TWI_ACK (1U << 2)
static int valid_instance(const I2C_TypeDef *instance)
{
    return instance == I2C0 || instance == I2C1;
}
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *handle)
{
    if (handle == NULL || !valid_instance(handle->Instance) || handle->Init.ClockSpeed == 0U ||
        handle->Init.ClockSpeed > 400000U || handle->Init.AddressingMode != I2C_ADDRESSINGMODE_7BIT)
    {
        return HAL_ERROR;
    }
    uint32_t pclk = HAL_RCC_GetPCLK2Freq(), best_divisor = UINT32_MAX, ccr = 0U;
    if (pclk == 0U)
    {
        return HAL_ERROR;
    }
    for (uint32_t n = 0U; n < 8U; ++n)
    {
        for (uint32_t m = 0U; m < 16U; ++m)
        {
            uint32_t divisor = 10U * (1UL << n) * (m + 1U);
            if ((uint64_t)handle->Init.ClockSpeed * divisor >= pclk && divisor < best_divisor)
            {
                best_divisor = divisor;
                ccr = (m << 3) | n;
            }
        }
    }
    if (best_divisor == UINT32_MAX)
    {
        return HAL_ERROR;
    }
    if (HAL_TryLock(&handle->Lock) != HAL_OK)
    {
        return HAL_BUSY;
    }
    if (handle->State == HAL_I2C_STATE_RESET)
    {
        HAL_I2C_MspInit(handle);
    }
    (void)HAL_RCC_I2C_EnableClock(handle->Instance);
    (void)HAL_RCC_I2C_ForceReset(handle->Instance);
    I2C_TypeDef *twi = handle->Instance;
    BUS_WRITE(twi->DRV_CTRL, 0U); /* Use TWI engine, not the separate FIFO driver. */
    BUS_WRITE(twi->SRST, 1U);
    HAL_StatusTypeDef status = bus_wait_clear(&twi->SRST, 1U);
    if (status == HAL_OK)
    {
        BUS_WRITE(twi->CCR, ccr);
        BUS_WRITE(twi->ADDR, 0U);
        BUS_WRITE(twi->XADDR, 0U);
        BUS_WRITE(twi->EFR, 0U);
        BUS_WRITE(twi->CNTR, TWI_BUS_EN | TWI_IF);
        handle->ActualClockSpeed = pclk / best_divisor;
    }
    handle->ErrorCode = status == HAL_OK ? HAL_I2C_ERROR_NONE : HAL_I2C_ERROR_TIMEOUT;
    handle->State = status == HAL_OK ? HAL_I2C_STATE_READY : HAL_I2C_STATE_RESET;
    HAL_Unlock(&handle->Lock);
    return status;
}
HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *handle)
{
    if (handle == NULL || !valid_instance(handle->Instance))
    {
        return HAL_ERROR;
    }
    if (HAL_TryLock(&handle->Lock) != HAL_OK)
    {
        return HAL_BUSY;
    }
    (void)HAL_RCC_I2C_ForceReset(handle->Instance);
    HAL_I2C_MspDeInit(handle);
    handle->State = HAL_I2C_STATE_RESET;
    HAL_Unlock(&handle->Lock);
    return HAL_OK;
}
static HAL_StatusTypeDef step(I2C_HandleTypeDef *handle, uint32_t control, uint32_t expected,
                              uint32_t start, uint32_t timeout)
{
    I2C_TypeDef *twi = handle->Instance;
    /* INT_FLAG is W1C. Never use read/modify/write for this register. */
    BUS_WRITE(twi->CNTR, TWI_BUS_EN | TWI_IF | control);
    while ((BUS_READ(twi->CNTR) & TWI_IF) == 0U)
    {
        if (bus_expired(start, timeout))
        {
            handle->ErrorCode |= HAL_I2C_ERROR_TIMEOUT;
            return HAL_TIMEOUT;
        }
    }
    uint32_t state = BUS_READ(twi->STAT) & 0xFFU;
    if (state == expected)
    {
        return HAL_OK;
    }
    if (state == 0x20U || state == 0x30U || state == 0x48U)
    {
        handle->ErrorCode |= HAL_I2C_ERROR_AF;
    }
    else if (state == 0x38U)
    {
        handle->ErrorCode |= HAL_I2C_ERROR_ARLO;
    }
    else
    {
        handle->ErrorCode |= HAL_I2C_ERROR_BERR;
    }
    return HAL_ERROR;
}
static HAL_StatusTypeDef address_phase(I2C_HandleTypeDef *handle, uint16_t address, int read,
                                       int repeated, uint32_t start, uint32_t timeout)
{
    HAL_StatusTypeDef status = step(handle, TWI_START, repeated ? 0x10U : 0x08U, start, timeout);
    if (status != HAL_OK)
    {
        return status;
    }
    BUS_WRITE(handle->Instance->DATA, address | (read ? 1U : 0U));
    return step(handle, 0U, read ? 0x40U : 0x18U, start, timeout);
}
static HAL_StatusTypeDef send_byte(I2C_HandleTypeDef *handle, uint8_t data, uint32_t start,
                                   uint32_t timeout)
{
    BUS_WRITE(handle->Instance->DATA, data);
    return step(handle, 0U, 0x28U, start, timeout);
}
static HAL_StatusTypeDef finish(I2C_HandleTypeDef *handle, HAL_StatusTypeDef status, uint32_t start,
                                uint32_t timeout)
{
    I2C_TypeDef *twi = handle->Instance;
    /* Never generate STOP after losing arbitration: the winner owns the bus. */
    if ((handle->ErrorCode & HAL_I2C_ERROR_ARLO) == 0U)
    {
        BUS_WRITE(twi->CNTR, TWI_BUS_EN | TWI_IF | TWI_STOP);
        while ((BUS_READ(twi->CNTR) & TWI_STOP) != 0U || (BUS_READ(twi->STAT) & 0xFFU) != 0xF8U)
        {
            if (bus_expired(start, timeout))
            {
                handle->ErrorCode |= HAL_I2C_ERROR_TIMEOUT;
                if (status == HAL_OK)
                {
                    status = HAL_TIMEOUT;
                }
                break;
            }
        }
    }
    else
    {
        BUS_WRITE(twi->CNTR, TWI_BUS_EN | TWI_IF);
    }
    if ((handle->ErrorCode & (HAL_I2C_ERROR_TIMEOUT | HAL_I2C_ERROR_BERR)) != 0U)
    {
        (void)HAL_RCC_I2C_ForceReset(twi);
        handle->State = HAL_I2C_STATE_RESET; /* Explicit Init required after a stuck bus. */
    }
    else
    {
        handle->State = HAL_I2C_STATE_READY;
    }
    HAL_Unlock(&handle->Lock);
    return status;
}
static HAL_StatusTypeDef transaction(I2C_HandleTypeDef *handle, uint16_t address,
                                     uint16_t mem_address, uint32_t mem_size, const uint8_t *tx,
                                     uint8_t *rx, uint32_t size, int read, uint32_t timeout)
{
    if (handle == NULL || !valid_instance(handle->Instance) ||
        handle->State == HAL_I2C_STATE_RESET || address > 0xFEU || (address & 1U) != 0U ||
        mem_size > 2U || (size != 0U && (read ? rx == NULL : tx == NULL)))
    {
        return HAL_ERROR;
    }
    if (HAL_TryLock(&handle->Lock) != HAL_OK)
    {
        return HAL_BUSY;
    }
    handle->State = HAL_I2C_STATE_BUSY;
    handle->ErrorCode = HAL_I2C_ERROR_NONE;
    uint32_t start = HAL_GetTick();
    HAL_StatusTypeDef status =
        address_phase(handle, address, read && mem_size == 0U, 0, start, timeout);
    if (status == HAL_OK && mem_size == 2U)
    {
        status = send_byte(handle, (uint8_t)(mem_address >> 8), start, timeout);
    }
    if (status == HAL_OK && mem_size != 0U)
    {
        status = send_byte(handle, (uint8_t)mem_address, start, timeout);
    }
    if (status == HAL_OK && mem_size != 0U && read)
    {
        status = address_phase(handle, address, 1, 1, start, timeout);
    }
    for (uint32_t i = 0U; status == HAL_OK && i < size; ++i)
    {
        if (read)
        {
            int last = i + 1U == size;
            status = step(handle, last ? 0U : TWI_ACK, last ? 0x58U : 0x50U, start, timeout);
            if (status == HAL_OK)
            {
                rx[i] = (uint8_t)BUS_READ(handle->Instance->DATA);
            }
        }
        else
        {
            status = send_byte(handle, tx[i], start, timeout);
        }
    }
    return finish(handle, status, start, timeout);
}
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *h, uint16_t a, const uint8_t *d,
                                          uint32_t s, uint32_t t)
{
    return transaction(h, a, 0U, 0U, d, NULL, s, 0, t);
}
HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *h, uint16_t a, uint8_t *d, uint32_t s,
                                         uint32_t t)
{
    if (s == 0U)
    {
        return HAL_ERROR;
    }
    return transaction(h, a, 0U, 0U, NULL, d, s, 1, t);
}
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *h, uint16_t a, uint16_t m, uint32_t ms,
                                    const uint8_t *d, uint32_t s, uint32_t t)
{
    if ((ms != 1U && ms != 2U) || (ms == 1U && m > 0xFFU))
    {
        return HAL_ERROR;
    }
    return transaction(h, a, m, ms, d, NULL, s, 0, t);
}
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *h, uint16_t a, uint16_t m, uint32_t ms,
                                   uint8_t *d, uint32_t s, uint32_t t)
{
    if (s == 0U || (ms != 1U && ms != 2U) || (ms == 1U && m > 0xFFU))
    {
        return HAL_ERROR;
    }
    return transaction(h, a, m, ms, NULL, d, s, 1, t);
}
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *h, uint16_t a, uint32_t trials,
                                        uint32_t timeout)
{
    if (trials == 0U)
    {
        return HAL_ERROR;
    }
    HAL_StatusTypeDef status = HAL_ERROR;
    for (uint32_t i = 0U; i < trials; ++i)
    {
        status = transaction(h, a, 0U, 0U, NULL, NULL, 0U, 0, timeout);
        if (status != HAL_ERROR || h == NULL || h->ErrorCode != HAL_I2C_ERROR_AF)
        {
            break;
        }
    }
    return status;
}
__weak void HAL_I2C_MspInit(I2C_HandleTypeDef *handle)
{
    UNUSED(handle);
}
__weak void HAL_I2C_MspDeInit(I2C_HandleTypeDef *handle)
{
    UNUSED(handle);
}
