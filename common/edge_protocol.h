#ifndef EDGE_PROTOCOL_H
#define EDGE_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EG_SOF1 0xAAu
#define EG_SOF2 0x55u
#define EG_PROTOCOL_VERSION 0x01u
#define EG_MAX_PAYLOAD 64u
#define EG_HEADER_SIZE 8u
#define EG_CRC_SIZE 2u
#define EG_MAX_FRAME_SIZE (EG_HEADER_SIZE + EG_MAX_PAYLOAD + EG_CRC_SIZE)

#define EG_SENSOR_VALID_TEMPERATURE_HUMIDITY 0x01u
#define EG_SENSOR_VALID_LIGHT 0x02u
#define EG_SENSOR_VALID_ANALOG 0x04u
#define EG_SENSOR_DATA_SYNTHETIC 0x80u

typedef enum {
    EG_MSG_SENSOR = 0x01,
    EG_MSG_HEARTBEAT = 0x02,
    EG_MSG_COMMAND = 0x10,
    EG_MSG_COMMAND_ACK = 0x11
} eg_message_type_t;

typedef enum {
    EG_CMD_SET_TEMP_THRESHOLD = 0x01,
    EG_CMD_ALARM_RESET = 0x02,
    EG_CMD_LED = 0x03,
    EG_CMD_BUZZER = 0x04
} eg_command_id_t;

typedef struct {
    int16_t temperature_centi_c;
    uint16_t humidity_centi_percent;
    uint16_t light_raw;
    uint16_t analog_raw;
    uint8_t valid_flags;
    uint8_t alarm;
} eg_sensor_payload_t;

typedef struct {
    uint8_t type;
    uint16_t sequence;
    uint16_t payload_length;
    uint8_t payload[EG_MAX_PAYLOAD];
} eg_message_t;

typedef enum {
    EG_PARSE_NONE = 0,
    EG_PARSE_FRAME = 1,
    EG_PARSE_BAD_CRC = -1,
    EG_PARSE_BAD_LENGTH = -2,
    EG_PARSE_BAD_VERSION = -3
} eg_parse_result_t;

typedef enum {
    EG_BUILD_OK = 0,
    EG_BUILD_NULL_ARGUMENT = -1,
    EG_BUILD_PAYLOAD_TOO_LARGE = -2,
    EG_BUILD_OUTPUT_TOO_SMALL = -3
} eg_build_result_t;

typedef struct {
    uint8_t bytes[EG_MAX_FRAME_SIZE];
    uint16_t used;
    uint16_t expected;
} eg_parser_t;

uint16_t eg_crc16_modbus(const uint8_t *data, size_t length);

size_t eg_build_frame(uint8_t type,
                      uint16_t sequence,
                      const uint8_t *payload,
                      uint16_t payload_length,
                      uint8_t *output,
                      size_t output_capacity);

eg_build_result_t eg_build_frame_ex(uint8_t type,
                                    uint16_t sequence,
                                    const uint8_t *payload,
                                    uint16_t payload_length,
                                    uint8_t *output,
                                    size_t output_capacity,
                                    size_t *written);

void eg_parser_init(eg_parser_t *parser);
eg_parse_result_t eg_parser_push(eg_parser_t *parser,
                                 uint8_t byte,
                                 eg_message_t *message);

void eg_encode_sensor(const eg_sensor_payload_t *sensor, uint8_t output[10]);
int eg_decode_sensor(const uint8_t *payload,
                     uint16_t payload_length,
                     eg_sensor_payload_t *sensor);

void eg_encode_command(uint8_t command_id, int32_t value, uint8_t output[5]);
int eg_decode_command(const uint8_t *payload,
                      uint16_t payload_length,
                      uint8_t *command_id,
                      int32_t *value);

#ifdef __cplusplus
}
#endif

#endif
