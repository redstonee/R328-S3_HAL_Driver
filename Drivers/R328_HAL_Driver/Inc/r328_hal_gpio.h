#ifndef R328_HAL_GPIO_H
#define R328_HAL_GPIO_H
#include "r328_hal_def.h"

#define GPIO_PIN_0 (1UL << 0)
#define GPIO_PIN_1 (1UL << 1)
#define GPIO_PIN_2 (1UL << 2)
#define GPIO_PIN_3 (1UL << 3)
#define GPIO_PIN_4 (1UL << 4)
#define GPIO_PIN_5 (1UL << 5)
#define GPIO_PIN_6 (1UL << 6)
#define GPIO_PIN_7 (1UL << 7)
#define GPIO_PIN_8 (1UL << 8)
#define GPIO_PIN_9 (1UL << 9)
#define GPIO_PIN_10 (1UL << 10)
#define GPIO_PIN_11 (1UL << 11)
#define GPIO_PIN_12 (1UL << 12)
#define GPIO_PIN_13 (1UL << 13)
#define GPIO_PIN_14 (1UL << 14)
#define GPIO_PIN_15 (1UL << 15)
#define GPIO_PIN_16 (1UL << 16)
#define GPIO_MODE_INPUT 0U
#define GPIO_MODE_OUTPUT_PP 1U
#define GPIO_MODE_AF_PP 2U  /* Alternate field selects the actual mux (2..6). */
#define GPIO_MODE_ANALOG 7U /* IO disabled, not an analog peripheral selection. */
#define GPIO_NOPULL 0U
#define GPIO_PULLUP 1U
#define GPIO_PULLDOWN 2U
#define GPIO_DRIVE_LEVEL0 0U
#define GPIO_DRIVE_LEVEL1 1U
#define GPIO_DRIVE_LEVEL2 2U
#define GPIO_DRIVE_LEVEL3 3U

typedef enum
{
    GPIO_PIN_RESET = 0,
    GPIO_PIN_SET
} GPIO_PinState;
typedef struct
{
    uint32_t Pin;
    uint32_t Mode;
    uint32_t Pull;
    uint32_t Drive;
    uint32_t Alternate;
} GPIO_InitTypeDef;

HAL_StatusTypeDef HAL_GPIO_Init(GPIO_TypeDef *port, const GPIO_InitTypeDef *init);
HAL_StatusTypeDef HAL_GPIO_DeInit(GPIO_TypeDef *port, uint32_t pins);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint32_t pins, GPIO_PinState state);
void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint32_t pins);
GPIO_PinState HAL_GPIO_ReadPin(const GPIO_TypeDef *port, uint32_t pins);

#define GPIO_EXTI_RISING 0U
#define GPIO_EXTI_FALLING 1U
#define GPIO_EXTI_HIGH 2U
#define GPIO_EXTI_LOW 3U
#define GPIO_EXTI_BOTH 4U

typedef struct
{
    uint32_t Pin;
    uint32_t Trigger;
    uint32_t Pull;
} GPIO_EXTI_InitTypeDef;

/* Call after HAL_Init(). Owns the bank GIC IRQ; only PB/PE/PG/PH support EINT.
 * Active pins must be deinitialized before GPIO mux/pull is reconfigured. */
HAL_StatusTypeDef HAL_GPIO_EXTI_Init(GPIO_TypeDef *port, const GPIO_EXTI_InitTypeDef *init);
HAL_StatusTypeDef HAL_GPIO_EXTI_DeInit(GPIO_TypeDef *port, uint32_t pins);
void HAL_GPIO_EXTI_IRQHandler(GPIO_TypeDef *port);
/* Called in IRQ context once per pending pin. Keep callbacks short. */
void HAL_GPIO_EXTI_Callback(GPIO_TypeDef *port, uint32_t pin);
#endif
