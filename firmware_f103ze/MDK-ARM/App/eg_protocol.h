#ifndef EG_PROTOCOL_H
#define EG_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define EG_SOF1              0xAAu
#define EG_SOF2              0x55u
#define EG_PROTOCOL_VERSION  0x01u
#define EG_MSG_SENSOR        0x01u
#define EG_SENSOR_PAYLOAD_SIZE 12u
#define EG_MAX_FRAME_SIZE    74u

#define EG_VALID_DHT11       0x01u
#define EG_VALID_NTC_ANALOG  0x02u
#define EG_VALID_ULTRASONIC  0x04u
#define EG_VALID_NTC_DIGITAL 0x08u

typedef struct {
    int16_t temperature_centi_c;
    uint16_t humidity_centi_percent;
    uint16_t ntc_adc_raw;
    uint16_t ntc_millivolts;
    uint16_t distance_mm;
    uint8_t valid_flags;
    uint8_t ntc_digital_level;
} EG_SensorPayload;

uint16_t EG_Protocol_Crc16(const uint8_t *data, size_t length);
void EG_Protocol_EncodeSensor(const EG_SensorPayload *sample,
                              uint8_t output[EG_SENSOR_PAYLOAD_SIZE]);
size_t EG_Protocol_BuildFrame(uint8_t type,
                              uint16_t sequence,
                              const uint8_t *payload,
                              uint16_t payload_length,
                              uint8_t *output,
                              size_t output_capacity);

#endif
