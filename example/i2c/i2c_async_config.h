#ifndef I2C_ASYNC_EXAMPLE_CONFIG_H
#define I2C_ASYNC_EXAMPLE_CONFIG_H

/* Shared by the standalone interrupt and DMA examples. Select your target. */
#define EXAMPLE_I2C_CLOCK_HZ 100000U
#define EXAMPLE_I2C_TIMEOUT_MS 100U
#define EXAMPLE_I2C_ADDRESS 0x50U /* Unshifted 7-bit address. */
#define EXAMPLE_I2C_REGISTER 0x00U
#define EXAMPLE_I2C_REGISTER_SIZE I2C_MEMADD_SIZE_8BIT
#define EXAMPLE_I2C_READ_SIZE 8U
/* t sends these bytes verbatim; w writes them at EXAMPLE_I2C_REGISTER.
 * No write is issued automatically. Adapt the payload to the target protocol. */
#define EXAMPLE_I2C_TX_DATA 0x55U, 0xAAU, 0x00U, 0xFFU
#endif
