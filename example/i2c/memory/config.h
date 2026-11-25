#ifndef I2C_MEMORY_EXAMPLE_CONFIG_H
#define I2C_MEMORY_EXAMPLE_CONFIG_H

/* Select a target and a readable register range before running this example. */
#define EXAMPLE_I2C_ADDRESS 0x50U
#define EXAMPLE_I2C_REGISTER 0x00U
#define EXAMPLE_I2C_REGISTER_SIZE I2C_MEMADD_SIZE_8BIT
#define EXAMPLE_I2C_READ_SIZE 8U
#endif
