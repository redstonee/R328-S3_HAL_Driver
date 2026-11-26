#ifndef BMP280_H
#define BMP280_H
#include <stdint.h>
#define BMP280_ADDRESS 0x76U /* SDO tied to GND. HAL callers shift this left once. */

#define BMP280_REG_CALIBRATION 0x88U
#define BMP280_REG_CHIP_ID 0xD0U
#define BMP280_REG_RESET 0xE0U
#define BMP280_REG_STATUS 0xF3U
#define BMP280_REG_CTRL_MEAS 0xF4U
#define BMP280_REG_CONFIG 0xF5U
#define BMP280_REG_PRESS_MSB 0xF7U

#define BMP280_CHIP_ID 0x58U
#define BMP280_RESET_COMMAND 0xB6U
#define BMP280_STATUS_IM_UPDATE (1U << 0)
#define BMP280_STATUS_MEASURING (1U << 3)
#define BMP280_CTRL_OSRS_T_X1 (1U << 5)
#define BMP280_CTRL_OSRS_P_X1 (1U << 2)
#define BMP280_CTRL_MODE_FORCED 0x01U
#define BMP280_CONFIG_FILTER_OFF (0U << 2)
#define BMP280_CONFIG_SPI3W_DISABLE 0U

#define BMP280_CALIBRATION_SIZE 24U
#define BMP280_SAMPLE_SIZE 6U
#define BMP280_TEMPERATURE_SCALE 100U /* Temperature is reported in 0.01 C. */
typedef struct
{
    uint16_t T1, P1;
    int16_t T2, T3, P2, P3, P4, P5, P6, P7, P8, P9;
} BMP280_Calibration;
int BMP280_DecodeCalibration(BMP280_Calibration *cal, const uint8_t raw[BMP280_CALIBRATION_SIZE]);
/* Temperature in 0.01 C, pressure in Pa. No floating-point printf required. */
int BMP280_Compensate(const BMP280_Calibration *cal, const uint8_t raw[BMP280_SAMPLE_SIZE],
                      int32_t *temperature_centi, uint32_t *pressure_pa);
#endif
