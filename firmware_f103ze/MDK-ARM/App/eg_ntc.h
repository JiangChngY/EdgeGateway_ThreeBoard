#ifndef EG_NTC_H
#define EG_NTC_H

#include "stm32f1xx_hal.h"

#include <stdint.h>

typedef struct {
    uint16_t adc_raw;
    uint16_t millivolts;
    uint8_t digital_level;
} EG_NTC_Sample;

HAL_StatusTypeDef EG_NTC_Init(void);
HAL_StatusTypeDef EG_NTC_Read(EG_NTC_Sample *sample);

#endif
