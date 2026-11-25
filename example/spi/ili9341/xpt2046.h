#ifndef EXAMPLE_XPT2046_H
#define EXAMPLE_XPT2046_H
#include "board.h"

/* Starting estimates only: record raw readings near each edge to calibrate.
 * Adjust axis swapping and inversion for the panel wiring and orientation. */
#define TOUCH_RAW_X_MIN 200U
#define TOUCH_RAW_X_MAX 3900U
#define TOUCH_RAW_Y_MIN 200U
#define TOUCH_RAW_Y_MAX 3900U
#define TOUCH_SWAP_XY 1U
#define TOUCH_INVERT_X 0U
#define TOUCH_INVERT_Y 0U

typedef struct
{
    uint16_t x, y;
    uint32_t pressed;
} XPT2046_Point;
HAL_StatusTypeDef XPT2046_Init(void);
HAL_StatusTypeDef XPT2046_ReadRaw(XPT2046_Point *point);
void XPT2046_ToScreen(const XPT2046_Point *raw, uint16_t *x, uint16_t *y);
#endif
