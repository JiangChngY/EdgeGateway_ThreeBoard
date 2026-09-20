#ifndef EG_DHT11_H
#define EG_DHT11_H

#include <stdint.h>

typedef enum {
    EG_DHT11_OK = 0,
    EG_DHT11_TIMEOUT = 1,
    EG_DHT11_CHECKSUM = 2,
    EG_DHT11_RANGE = 3
} EG_DHT11_Status;

typedef struct {
    int16_t temperature_centi_c;
    uint16_t humidity_centi_percent;
} EG_DHT11_Sample;

EG_DHT11_Status EG_DHT11_Read(EG_DHT11_Sample *sample);

#endif
