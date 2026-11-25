#include "r328_hal.h"

static TIM_HandleTypeDef *owners[2];
static int tim_index(const TIM_TypeDef *instance)
{
    if (instance == TIM0)
    {
        return 0;
    }
    if (instance == TIM1)
    {
        return 1;
    }
    return -1;
}
static int owns_timer(const TIM_HandleTypeDef *handle)
{
    if (handle == NULL)
    {
        return 0;
    }
    int index = tim_index(handle->Instance);
    return index >= 0 && owners[index] == handle;
}
static IRQn_Type tim_irq(const TIM_HandleTypeDef *handle)
{
    return handle->Instance == TIM0 ? TIM0_IRQn : TIM1_IRQn;
}
static void dispatch(void *context)
{
    HAL_TIM_IRQHandler((TIM_HandleTypeDef *)context);
}

HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef *handle)
{
    if (handle == NULL || tim_index(handle->Instance) < 0 || handle->Init.Prescaler > 7U ||
        handle->Init.Mode > 1U || handle->Init.Period == 0U)
    {
        return HAL_ERROR;
    }
    uint32_t state = R328_IRQ_Save();
    int index = tim_index(handle->Instance);
    if ((owners[index] != NULL && owners[index] != handle) || handle->State == HAL_TIM_STATE_BUSY ||
        HAL_TryLock(&handle->Lock) != HAL_OK)
    {
        R328_IRQ_Restore(state);
        return HAL_BUSY;
    }
    owners[index] = handle;
    handle->Instance->CTRL = 4U | (handle->Init.Prescaler << 4) | (handle->Init.Mode << 7);
    handle->Instance->INTV = handle->Init.Period;
    TIMER->IRQ_STA = 1UL << (uint32_t)index;
    handle->State = HAL_TIM_STATE_READY;
    R328_DSB();
    HAL_Unlock(&handle->Lock);
    R328_IRQ_Restore(state);
    return HAL_OK;
}
static HAL_StatusTypeDef start(TIM_HandleTypeDef *handle, int interrupt)
{
    if (!owns_timer(handle))
    {
        return HAL_ERROR;
    }
    if (HAL_TryLock(&handle->Lock) != HAL_OK)
    {
        return HAL_BUSY;
    }
    if (handle->State != HAL_TIM_STATE_READY)
    {
        HAL_Unlock(&handle->Lock);
        return HAL_BUSY;
    }
    uint32_t bit = 1UL << (uint32_t)tim_index(handle->Instance);
    if (interrupt != 0)
    {
        HAL_StatusTypeDef status =
            HAL_GIC_RegisterIRQ(tim_irq(handle), dispatch, handle, HAL_TIM_IRQ_PRIORITY);
        if (status != HAL_OK)
        {
            HAL_Unlock(&handle->Lock);
            return status;
        }
    }
    handle->Instance->CTRL |= 2U;
    uint32_t remaining = HAL_REGISTER_POLL_LIMIT;
    while ((handle->Instance->CTRL & 2U) != 0U)
    {
        if (--remaining == 0U)
        {
            handle->Instance->CTRL &= ~3UL;
            if (interrupt != 0)
            {
                (void)HAL_GIC_UnregisterIRQ(tim_irq(handle), handle);
            }
            HAL_Unlock(&handle->Lock);
            return HAL_TIMEOUT;
        }
    }
    TIMER->IRQ_STA = bit;
    if (interrupt != 0)
    {
        TIMER->IRQ_EN |= bit;
        HAL_StatusTypeDef status = HAL_GIC_EnableIRQ(tim_irq(handle));
        if (status != HAL_OK)
        {
            (void)HAL_GIC_UnregisterIRQ(tim_irq(handle), handle);
            HAL_Unlock(&handle->Lock);
            return status;
        }
    }
    uint32_t state = R328_IRQ_Save();
    handle->State = HAL_TIM_STATE_BUSY;
    handle->Instance->CTRL |= 1U;
    R328_DSB();
    HAL_Unlock(&handle->Lock);
    R328_IRQ_Restore(state);
    return HAL_OK;
}
HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *handle)
{
    return start(handle, 0);
}
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *handle)
{
    return start(handle, 1);
}
HAL_StatusTypeDef HAL_TIM_Base_Stop(TIM_HandleTypeDef *handle)
{
    if (!owns_timer(handle))
    {
        return HAL_ERROR;
    }
    if (HAL_TryLock(&handle->Lock) != HAL_OK)
    {
        return HAL_BUSY;
    }
    uint32_t state = R328_IRQ_Save();
    HAL_GIC_DisableIRQ(tim_irq(handle));
    handle->Instance->CTRL &= ~3UL;
    R328_DSB();
    /* Wait >= 2 OSC24M timer cycles before allowing a restart (manual 3.6.6.3).
       APB register accesses below also serialize the stop. */
    for (volatile uint32_t wait = 0U; wait < 8192U; ++wait)
    {
    }
    TIMER->IRQ_STA = 1UL << (uint32_t)tim_index(handle->Instance);
    handle->State = HAL_TIM_STATE_READY;
    HAL_Unlock(&handle->Lock);
    R328_IRQ_Restore(state);
    return HAL_OK;
}
HAL_StatusTypeDef HAL_TIM_Base_Stop_IT(TIM_HandleTypeDef *handle)
{
    return HAL_TIM_Base_Stop(handle);
}
HAL_StatusTypeDef HAL_TIM_Base_DeInit(TIM_HandleTypeDef *handle)
{
    HAL_StatusTypeDef status = HAL_TIM_Base_Stop(handle);
    if (status != HAL_OK)
    {
        return status;
    }
    uint32_t state = R328_IRQ_Save();
    status = HAL_GIC_UnregisterIRQ(tim_irq(handle), handle);
    if (status == HAL_OK)
    {
        owners[tim_index(handle->Instance)] = NULL;
        handle->State = HAL_TIM_STATE_RESET;
    }
    R328_IRQ_Restore(state);
    return status;
}
void HAL_TIM_IRQHandler(TIM_HandleTypeDef *handle)
{
    if (!owns_timer(handle))
    {
        return;
    }
    uint32_t bit = 1UL << (uint32_t)tim_index(handle->Instance);
    if ((TIMER->IRQ_STA & bit) == 0U)
    {
        return;
    }
    TIMER->IRQ_STA = bit; /* W1C: never use |= here. */
    R328_DSB();
    if (handle->Init.Mode == TIM_MODE_ONESHOT)
    {
        HAL_GIC_DisableIRQ(tim_irq(handle));
        handle->State = HAL_TIM_STATE_READY;
    }
    if (handle->PeriodElapsedCallback != NULL)
    {
        handle->PeriodElapsedCallback(handle);
    }
    else
    {
        HAL_TIM_PeriodElapsedCallback(handle);
    }
}
__weak void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *handle)
{
    UNUSED(handle);
}
