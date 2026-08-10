#include "edge_protocol.h"

#include <string.h>

static void write_u16_le(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static uint16_t read_u16_le(const uint8_t *in)
{
    return (uint16_t)((uint16_t)in[0] | ((uint16_t)in[1] << 8));
}

static void write_i32_le(uint8_t *out, int32_t value)
{
    uint32_t raw = (uint32_t)value;
    out[0] = (uint8_t)(raw & 0xFFu);
    out[1] = (uint8_t)((raw >> 8) & 0xFFu);
    out[2] = (uint8_t)((raw >> 16) & 0xFFu);
    out[3] = (uint8_t)((raw >> 24) & 0xFFu);
}

static int32_t read_i32_le(const uint8_t *in)
{
    uint32_t raw = (uint32_t)in[0]
                 | ((uint32_t)in[1] << 8)
                 | ((uint32_t)in[2] << 16)
                 | ((uint32_t)in[3] << 24);
    return (int32_t)raw;
}

uint16_t eg_crc16_modbus(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFu;
    size_t i;
    uint8_t bit;

    for (i = 0; i < length; ++i) {
        crc ^= data[i];
        for (bit = 0; bit < 8u; ++bit) {
            if ((crc & 1u) != 0u) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001u);
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

eg_build_result_t eg_build_frame_ex(uint8_t type,
                                    uint16_t sequence,
                                    const uint8_t *payload,
                                    uint16_t payload_length,
                                    uint8_t *output,
                                    size_t output_capacity,
                                    size_t *written)
{
    size_t frame_length;
    uint16_t crc;

    if (written == NULL) {
        return EG_BUILD_NULL_ARGUMENT;
    }
    *written = 0u;
    if (output == NULL || (payload_length > 0u && payload == NULL)) {
        return EG_BUILD_NULL_ARGUMENT;
    }
    if (payload_length > EG_MAX_PAYLOAD) {
        return EG_BUILD_PAYLOAD_TOO_LARGE;
    }

    frame_length = EG_HEADER_SIZE + payload_length + EG_CRC_SIZE;
    if (output_capacity < frame_length) {
        return EG_BUILD_OUTPUT_TOO_SMALL;
    }

    output[0] = EG_SOF1;
    output[1] = EG_SOF2;
    output[2] = EG_PROTOCOL_VERSION;
    output[3] = type;
    write_u16_le(&output[4], sequence);
    write_u16_le(&output[6], payload_length);
    if (payload_length > 0u) {
        memcpy(&output[8], payload, payload_length);
    }

    crc = eg_crc16_modbus(&output[2], (size_t)6u + payload_length);
    write_u16_le(&output[8u + payload_length], crc);
    *written = frame_length;
    return EG_BUILD_OK;
}

size_t eg_build_frame(uint8_t type,
                      uint16_t sequence,
                      const uint8_t *payload,
                      uint16_t payload_length,
                      uint8_t *output,
                      size_t output_capacity)
{
    size_t written = 0u;
    if (eg_build_frame_ex(type, sequence, payload, payload_length,
                          output, output_capacity, &written) != EG_BUILD_OK) {
        return 0u;
    }
    return written;
}

void eg_parser_init(eg_parser_t *parser)
{
    if (parser != NULL) {
        parser->used = 0u;
        parser->expected = 0u;
    }
}

static void parser_restart(eg_parser_t *parser, uint8_t current)
{
    parser->used = 0u;
    parser->expected = 0u;
    if (current == EG_SOF1) {
        parser->bytes[0] = current;
        parser->used = 1u;
    }
}

eg_parse_result_t eg_parser_push(eg_parser_t *parser,
                                 uint8_t byte,
                                 eg_message_t *message)
{
    uint16_t payload_length;
    uint16_t received_crc;
    uint16_t calculated_crc;

    if (parser == NULL || message == NULL) {
        return EG_PARSE_BAD_LENGTH;
    }

    if (parser->used == 0u) {
        if (byte == EG_SOF1) {
            parser->bytes[0] = byte;
            parser->used = 1u;
        }
        return EG_PARSE_NONE;
    }

    if (parser->used == 1u) {
        if (byte == EG_SOF2) {
            parser->bytes[1] = byte;
            parser->used = 2u;
        } else {
            parser_restart(parser, byte);
        }
        return EG_PARSE_NONE;
    }

    if (parser->used >= EG_MAX_FRAME_SIZE) {
        parser_restart(parser, byte);
        return EG_PARSE_BAD_LENGTH;
    }

    parser->bytes[parser->used++] = byte;

    if (parser->used == EG_HEADER_SIZE) {
        if (parser->bytes[2] != EG_PROTOCOL_VERSION) {
            eg_parser_init(parser);
            return EG_PARSE_BAD_VERSION;
        }
        payload_length = read_u16_le(&parser->bytes[6]);
        if (payload_length > EG_MAX_PAYLOAD) {
            eg_parser_init(parser);
            return EG_PARSE_BAD_LENGTH;
        }
        parser->expected = (uint16_t)(EG_HEADER_SIZE + payload_length + EG_CRC_SIZE);
    }

    if (parser->expected == 0u || parser->used < parser->expected) {
        return EG_PARSE_NONE;
    }

    payload_length = read_u16_le(&parser->bytes[6]);
    received_crc = read_u16_le(&parser->bytes[8u + payload_length]);
    calculated_crc = eg_crc16_modbus(&parser->bytes[2], (size_t)6u + payload_length);
    if (received_crc != calculated_crc) {
        eg_parser_init(parser);
        return EG_PARSE_BAD_CRC;
    }

    message->type = parser->bytes[3];
    message->sequence = read_u16_le(&parser->bytes[4]);
    message->payload_length = payload_length;
    if (payload_length > 0u) {
        memcpy(message->payload, &parser->bytes[8], payload_length);
    }
    eg_parser_init(parser);
    return EG_PARSE_FRAME;
}

void eg_encode_sensor(const eg_sensor_payload_t *sensor, uint8_t output[10])
{
    write_u16_le(&output[0], (uint16_t)sensor->temperature_centi_c);
    write_u16_le(&output[2], sensor->humidity_centi_percent);
    write_u16_le(&output[4], sensor->light_raw);
    write_u16_le(&output[6], sensor->analog_raw);
    output[8] = sensor->valid_flags;
    output[9] = sensor->alarm;
}

int eg_decode_sensor(const uint8_t *payload,
                     uint16_t payload_length,
                     eg_sensor_payload_t *sensor)
{
    if (payload == NULL || sensor == NULL || payload_length != 10u) {
        return 0;
    }
    sensor->temperature_centi_c = (int16_t)read_u16_le(&payload[0]);
    sensor->humidity_centi_percent = read_u16_le(&payload[2]);
    sensor->light_raw = read_u16_le(&payload[4]);
    sensor->analog_raw = read_u16_le(&payload[6]);
    sensor->valid_flags = payload[8];
    sensor->alarm = payload[9];
    return 1;
}

void eg_encode_command(uint8_t command_id, int32_t value, uint8_t output[5])
{
    output[0] = command_id;
    write_i32_le(&output[1], value);
}

int eg_decode_command(const uint8_t *payload,
                      uint16_t payload_length,
                      uint8_t *command_id,
                      int32_t *value)
{
    if (payload == NULL || command_id == NULL || value == NULL || payload_length != 5u) {
        return 0;
    }
    *command_id = payload[0];
    *value = read_i32_le(&payload[1]);
    return 1;
}
