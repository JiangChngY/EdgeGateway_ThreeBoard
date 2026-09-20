#include "eg_protocol.h"

static void write_u16_le(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)(value & 0xFFu);
    output[1] = (uint8_t)(value >> 8);
}

uint16_t EG_Protocol_Crc16(const uint8_t *data, size_t length)
{
    uint16_t crc;
    size_t index;
    uint8_t bit;

    crc = 0xFFFFu;
    for (index = 0u; index < length; ++index) {
        crc ^= data[index];
        for (bit = 0u; bit < 8u; ++bit) {
            if ((crc & 1u) != 0u) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001u);
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

void EG_Protocol_EncodeSensor(const EG_SensorPayload *sample,
                              uint8_t output[EG_SENSOR_PAYLOAD_SIZE])
{
    write_u16_le(&output[0], (uint16_t)sample->temperature_centi_c);
    write_u16_le(&output[2], sample->humidity_centi_percent);
    write_u16_le(&output[4], sample->ntc_adc_raw);
    write_u16_le(&output[6], sample->ntc_millivolts);
    write_u16_le(&output[8], sample->distance_mm);
    output[10] = sample->valid_flags;
    output[11] = sample->ntc_digital_level;
}

size_t EG_Protocol_BuildFrame(uint8_t type,
                              uint16_t sequence,
                              const uint8_t *payload,
                              uint16_t payload_length,
                              uint8_t *output,
                              size_t output_capacity)
{
    size_t frame_length;
    uint16_t crc;
    uint16_t index;

    frame_length = (size_t)8u + payload_length + 2u;
    if (output == NULL || payload == NULL || payload_length > 64u ||
        output_capacity < frame_length) {
        return 0u;
    }

    output[0] = EG_SOF1;
    output[1] = EG_SOF2;
    output[2] = EG_PROTOCOL_VERSION;
    output[3] = type;
    write_u16_le(&output[4], sequence);
    write_u16_le(&output[6], payload_length);
    for (index = 0u; index < payload_length; ++index) {
        output[8u + index] = payload[index];
    }
    crc = EG_Protocol_Crc16(&output[2], (size_t)6u + payload_length);
    write_u16_le(&output[8u + payload_length], crc);
    return frame_length;
}
