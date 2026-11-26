#ifndef R328_HAL_CONF_H
#define R328_HAL_CONF_H
/* Initial module-wide polling deadline in milliseconds. */
#define HAL_POLLING_DEFAULT_TIMEOUT_MS 100U
/* Max register-poll iterations before the millisecond timebase is available. */
#define HAL_REGISTER_POLL_LIMIT 1000000U
#define HAL_TIM_IRQ_PRIORITY 0x80U
#endif
