#include "stm32f10x.h"

#include "app_config.h"
#include "bsp_adc.h"
#include "bsp_dht11.h"
#include "bsp_outputs.h"
#include "bsp_time.h"
#include "bsp_uart.h"
#include "bsp_watchdog.h"
#include "edge_protocol.h"

static eg_parser_t s_parser;
static uint16_t s_sequence;
static int16_t s_temperature = 2500;
static uint16_t s_humidity = 5500u;
static int32_t s_temperature_threshold = EG_DEFAULT_TEMP_THRESHOLD_CENTI;
static uint8_t s_alarm;
static uint8_t s_alarm_silenced;
static uint8_t s_manual_buzzer;
static uint8_t s_valid_flags = EG_SENSOR_VALID_LIGHT | EG_SENSOR_VALID_ANALOG;

static void apply_buzzer_output(void)
{
    BSP_Buzzer_Set((s_alarm != 0u) || (s_manual_buzzer != 0u));
}

static void send_frame(uint8_t type, const uint8_t *payload, uint16_t length)
{
    uint8_t frame[EG_MAX_FRAME_SIZE];
    size_t frame_length = eg_build_frame(type, ++s_sequence, payload, length,
                                         frame, sizeof(frame));
    if (frame_length > 0u) {
        BSP_UART_Write(frame, frame_length);
    }
}

static void send_command_ack(uint8_t command_id, uint8_t status)
{
    uint8_t payload[2];
    payload[0] = command_id;
    payload[1] = status;
    send_frame(EG_MSG_COMMAND_ACK, payload, sizeof(payload));
}

static void handle_command(const eg_message_t *message)
{
    uint8_t command_id;
    int32_t value;
    uint8_t status = 0u;

    if (message->type != EG_MSG_COMMAND ||
        !eg_decode_command(message->payload, message->payload_length,
                           &command_id, &value)) {
        return;
    }

    switch (command_id) {
    case EG_CMD_SET_TEMP_THRESHOLD:
        if (value >= -2000 && value <= 8000) {
            s_temperature_threshold = value;
            s_alarm = 0u;
            s_alarm_silenced = 0u;
            apply_buzzer_output();
        } else {
            status = 1u;
        }
        break;
    case EG_CMD_ALARM_RESET:
        s_alarm = 0u;
        s_alarm_silenced = 1u;
        s_manual_buzzer = 0u;
        apply_buzzer_output();
        break;
    case EG_CMD_LED:
        BSP_LED_Set(value != 0);
        break;
    case EG_CMD_BUZZER:
        s_manual_buzzer = (uint8_t)(value != 0);
        apply_buzzer_output();
        break;
    default:
        status = 2u;
        break;
    }
    send_command_ack(command_id, status);
}

static void poll_uart(void)
{
    uint8_t value;
    eg_message_t message;
    while (BSP_UART_ReadByte(&value)) {
        if (eg_parser_push(&s_parser, value, &message) == EG_PARSE_FRAME) {
            handle_command(&message);
        }
    }
}

static void update_dht(void)
{
    int ok = 0;
#if EG_ENABLE_DHT11
    ok = BSP_DHT11_Read(&s_temperature, &s_humidity);
#endif
    if (ok) {
        s_valid_flags |= EG_SENSOR_VALID_TEMPERATURE_HUMIDITY;
        s_valid_flags &= (uint8_t)~EG_SENSOR_DATA_SYNTHETIC;
    }
#if EG_USE_SYNTHETIC_ON_DHT_FAILURE
    if (!ok) {
        uint16_t analog = BSP_ADC_AnalogRaw();
        s_temperature = (int16_t)(2200 + (analog * 1200u) / 4095u);
        s_humidity = (uint16_t)(4500u + (BSP_ADC_LightRaw() * 2500u) / 4095u);
        s_valid_flags |= EG_SENSOR_VALID_TEMPERATURE_HUMIDITY;
        s_valid_flags |= EG_SENSOR_DATA_SYNTHETIC;
    }
#else
    if (!ok) {
        s_valid_flags &= (uint8_t)~EG_SENSOR_VALID_TEMPERATURE_HUMIDITY;
        s_valid_flags &= (uint8_t)~EG_SENSOR_DATA_SYNTHETIC;
    }
#endif
}

static void update_alarm_state(void)
{
    if ((s_valid_flags & EG_SENSOR_VALID_TEMPERATURE_HUMIDITY) == 0u) {
        s_alarm = 0u;
        s_alarm_silenced = 0u;
    } else if (s_temperature <
               (s_temperature_threshold - EG_ALARM_HYSTERESIS_CENTI)) {
        s_alarm = 0u;
        s_alarm_silenced = 0u;
    } else if (s_temperature >= s_temperature_threshold &&
               s_alarm_silenced == 0u) {
        s_alarm = 1u;
    }
    apply_buzzer_output();
}

static void send_sensor_sample(void)
{
    eg_sensor_payload_t sensor;
    uint8_t payload[10];

    update_alarm_state();

    sensor.temperature_centi_c = s_temperature;
    sensor.humidity_centi_percent = s_humidity;
    sensor.light_raw = BSP_ADC_LightRaw();
    sensor.analog_raw = BSP_ADC_AnalogRaw();
    sensor.valid_flags = s_valid_flags;
    sensor.alarm = s_alarm;
    eg_encode_sensor(&sensor, payload);
    send_frame(EG_MSG_SENSOR, payload, sizeof(payload));
}

int main(void)
{
    uint32_t last_sensor = 0u;
    uint32_t last_heartbeat = 0u;
    uint32_t last_dht = 0u;

    BSP_Time_Init();
    BSP_Outputs_Init();
    BSP_ADC_Init();
    BSP_DHT11_Init();
    BSP_UART_Init(EG_UART_BAUDRATE);
    eg_parser_init(&s_parser);
    BSP_Watchdog_Init();

    while (1) {
        uint32_t now = BSP_Time_Millis();
        poll_uart();

        if ((uint32_t)(now - last_dht) >= EG_DHT_PERIOD_MS) {
            last_dht = now;
            update_dht();
        }
        if ((uint32_t)(now - last_sensor) >= EG_SENSOR_PERIOD_MS) {
            last_sensor = now;
            send_sensor_sample();
        }
        if ((uint32_t)(now - last_heartbeat) >= EG_HEARTBEAT_PERIOD_MS) {
            last_heartbeat = now;
            send_frame(EG_MSG_HEARTBEAT, NULL, 0u);
        }
        BSP_Watchdog_Feed();
    }
}
