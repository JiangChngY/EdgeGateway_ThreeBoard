#ifndef EG_TIME_H
#define EG_TIME_H

#include "stm32f1xx_hal.h"

HAL_StatusTypeDef EG_Time_Init(void);
uint16_t EG_Time_Micros16(void);
void EG_Time_DelayUs(uint16_t microseconds);

#endif
