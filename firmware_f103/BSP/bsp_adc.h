#ifndef BSP_ADC_H
#define BSP_ADC_H

#include <stdint.h>

void BSP_ADC_Init(void);
uint16_t BSP_ADC_LightRaw(void);
uint16_t BSP_ADC_AnalogRaw(void);

#endif

