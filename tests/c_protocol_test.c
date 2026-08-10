#include "edge_protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    static const uint8_t crc_vector[] = "123456789";
    eg_sensor_payload_t input;
    eg_sensor_payload_t decoded;
    eg_message_t message;
    eg_parser_t parser;
    uint8_t payload[10];
    uint8_t frame[EG_MAX_FRAME_SIZE];
    size_t length = 0u;
    size_t i;
    eg_parse_result_t parse_result = EG_PARSE_NONE;

    assert(eg_crc16_modbus(crc_vector, 9u) == 0x4B37u);

    input.temperature_centi_c = 2534;
    input.humidity_centi_percent = 6123u;
    input.light_raw = 1234u;
    input.analog_raw = 2345u;
    input.valid_flags = EG_SENSOR_VALID_TEMPERATURE_HUMIDITY |
                        EG_SENSOR_VALID_LIGHT |
                        EG_SENSOR_VALID_ANALOG;
    input.alarm = 1u;
    eg_encode_sensor(&input, payload);

    assert(eg_build_frame_ex(EG_MSG_SENSOR, 42u, payload, sizeof(payload),
                             frame, sizeof(frame), &length) == EG_BUILD_OK);
    assert(length == 20u);
    assert(eg_build_frame_ex(EG_MSG_SENSOR, 42u, payload, EG_MAX_PAYLOAD + 1u,
                             frame, sizeof(frame), &length) == EG_BUILD_PAYLOAD_TOO_LARGE);
    assert(eg_build_frame_ex(EG_MSG_SENSOR, 42u, payload, sizeof(payload),
                             frame, 5u, &length) == EG_BUILD_OUTPUT_TOO_SMALL);
    assert(eg_build_frame_ex(EG_MSG_SENSOR, 42u, payload, sizeof(payload),
                             NULL, sizeof(frame), &length) == EG_BUILD_NULL_ARGUMENT);

    assert(eg_build_frame_ex(EG_MSG_SENSOR, 42u, payload, sizeof(payload),
                             frame, sizeof(frame), &length) == EG_BUILD_OK);
    eg_parser_init(&parser);
    for (i = 0u; i < length; ++i) {
        parse_result = eg_parser_push(&parser, frame[i], &message);
    }
    assert(parse_result == EG_PARSE_FRAME);
    assert(message.type == EG_MSG_SENSOR);
    assert(message.sequence == 42u);
    assert(eg_decode_sensor(message.payload, message.payload_length, &decoded));
    assert(memcmp(&input, &decoded, sizeof(input)) == 0);

    puts("C protocol tests passed");
    return 0;
}
