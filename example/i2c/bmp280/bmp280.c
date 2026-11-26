#include "bmp280.h"
#include <stddef.h>

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8);
}
int BMP280_DecodeCalibration(BMP280_Calibration *c, const uint8_t raw[BMP280_CALIBRATION_SIZE])
{
    if (c == NULL || raw == NULL)
    {
        return 0;
    }
    c->T1 = le16(raw);
    c->T2 = (int16_t)le16(raw + 2);
    c->T3 = (int16_t)le16(raw + 4);
    c->P1 = le16(raw + 6);
    c->P2 = (int16_t)le16(raw + 8);
    c->P3 = (int16_t)le16(raw + 10);
    c->P4 = (int16_t)le16(raw + 12);
    c->P5 = (int16_t)le16(raw + 14);
    c->P6 = (int16_t)le16(raw + 16);
    c->P7 = (int16_t)le16(raw + 18);
    c->P8 = (int16_t)le16(raw + 20);
    c->P9 = (int16_t)le16(raw + 22);
    return c->T1 != 0U && c->T1 != 0xFFFFU && c->P1 != 0U && c->P1 != 0xFFFFU;
}
int BMP280_Compensate(const BMP280_Calibration *c, const uint8_t raw[BMP280_SAMPLE_SIZE],
                      int32_t *temperature, uint32_t *pressure)
{
    if (c == NULL || raw == NULL || temperature == NULL || pressure == NULL || c->P1 == 0U)
    {
        return 0;
    }
    uint32_t adc_p = (uint32_t)raw[0] << 12 | (uint32_t)raw[1] << 4 | raw[2] >> 4;
    uint32_t adc_t = (uint32_t)raw[3] << 12 | (uint32_t)raw[4] << 4 | raw[5] >> 4;
    if (adc_p == 0x80000U || adc_t == 0x80000U)
    {
        return 0;
    } /* Measurement disabled. */
    /* Bosch BMP280 datasheet 3.11/appendix 8.1 compensation equations.
     * Software double arithmetic needs only libgcc, without libm or an FPU. */
    double offset = (double)adc_t / 131072.0 - (double)c->T1 / 8192.0;
    double fine =
        ((double)adc_t / 16384.0 - (double)c->T1 / 1024.0) * c->T2 + offset * offset * c->T3;
    double t = fine / 5120.0;
    double a = fine / 2.0 - 64000.0;
    double b = a * a * c->P6 / 32768.0 + a * c->P5 * 2.0;
    b = b / 4.0 + c->P4 * 65536.0;
    a = (c->P3 * a * a / 524288.0 + c->P2 * a) / 524288.0;
    a = (1.0 + a / 32768.0) * c->P1;
    if (a <= 0.0)
    {
        return 0;
    }
    double p = (1048576.0 - adc_p - b / 4096.0) * 6250.0 / a;
    p += (c->P9 * p * p / 2147483648.0 + p * c->P8 / 32768.0 + c->P7) / 16.0;
    if (!(t >= -40.0 && t <= 85.0 && p >= 30000.0 && p <= 110000.0))
    {
        return 0;
    }
    *temperature = (int32_t)(t * BMP280_TEMPERATURE_SCALE + (t < 0.0 ? -0.5 : 0.5));
    *pressure = (uint32_t)(p + 0.5);
    return 1;
}
