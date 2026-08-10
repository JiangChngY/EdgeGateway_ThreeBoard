#ifndef BSP_DHT11_H
#define BSP_DHT11_H

#include <stdint.h>

void BSP_DHT11_Init(void);
int BSP_DHT11_Read(int16_t *temperature_centi_c,
                   uint16_t *humidity_centi_percent);

#endif

