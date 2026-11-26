#include "bmp280.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    /* Independent factory-compensation reference vector, 25.08 C / 100653 Pa. */
    const uint16_t words[] = {27504, 26435, (uint16_t)-1000, 36477, (uint16_t)-10685, 3024,
                              2855,  140,   (uint16_t)-7,    15500, (uint16_t)-14600, 6000};
    uint8_t raw[24];
    for (uint32_t i = 0U; i < 12U; ++i)
    {
        raw[i * 2U] = (uint8_t)words[i];
        raw[i * 2U + 1U] = (uint8_t)(words[i] >> 8);
    }
    BMP280_Calibration c;
    assert(BMP280_DecodeCalibration(&c, raw));
    assert(c.T3 == -1000 && c.P2 == -10685 && c.P6 == -7 && c.P8 == -14600);
    const uint8_t sample[] = {0x65, 0x5A, 0xC0, 0x7E, 0xED, 0x00};
    int32_t t;
    uint32_t p;
    assert(BMP280_Compensate(&c, sample, &t, &p));
    assert(t == 2508 && p == 100653U);
    const uint8_t disabled[] = {0x80, 0x00, 0x00, 0x7E, 0xED, 0x00};
    assert(!BMP280_Compensate(&c, disabled, &t, &p));
    c.P1 = 0U;
    assert(!BMP280_Compensate(&c, sample, &t, &p));
    memset(raw, 0xFF, sizeof(raw));
    assert(!BMP280_DecodeCalibration(&c, raw));
    memset(raw, 0, sizeof(raw));
    assert(!BMP280_DecodeCalibration(&c, raw));
    puts("BMP280 calibration, compensation and invalid-sample checks passed");
    return 0;
}
