#include "edge_gateway_app.h"

#include "eg_dht11.h"
#include "eg_ntc.h"
#include "eg_protocol.h"
#include "eg_time.h"
#include "eg_ultrasonic.h"
#include "usart.h"

#include <stdio.h>
#include <string.h>

#define EG_NTC_PERIOD_MS        200u
#define EG_ULTRASONIC_PERIOD_MS 250u
#define EG_DHT11_PERIOD_MS      2000u
#define EG_PUBLISH_PERIOD_MS    1000u

static EG_SensorPayload s_sample;
static uint16_t s_sequence = 0u;
static uint32_t s_next_ntc_ms = 0u;
static uint32_t s_next_ultrasonic_ms = 0u;
static uint32_t s_next_dht11_ms = 0u;
static uint32_t s_next_publish_ms = 0u;
static uint32_t s_ntc_errors = 0u;
static uint32_t s_dht_errors = 0u;
static uint32_t s_ultrasonic_errors = 0u;

static uint8_t time_reached(uint32_t now, uint32_t deadline)
{
    return ((int32_t)(now - deadline) >= 0) ? 1u : 0u;
}

static void send_debug_line(void)
{
    char line[192];
    int length;
    int32_t temperature;
    int32_t temperature_abs;
    const char *temperature_sign;
    uint32_t humidity;

    temperature = s_sample.temperature_centi_c;
    temperature_abs = (temperature < 0) ? -temperature : temperature;
    temperature_sign = (temperature < 0) ? "-" : "";
    humidity = s_sample.humidity_centi_percent;
    length = snprintf(line, sizeof(line),
                      "[EG] seq=%u DHT=%s%s%ld.%02ldC/%s%u.%02u%% "
                      "NTC=%u,%umV,DO=%u US=%s%umm flags=0x%02X "
                      "err=%lu/%lu/%lu\r\n",
                      (unsigned int)s_sequence,
                      ((s_sample.valid_flags & EG_VALID_DHT11) != 0u) ? "" : "INVALID,",
                      temperature_sign,
                      (long)(temperature_abs / 100),
                      (long)(temperature_abs % 100),
                      ((s_sample.valid_flags & EG_VALID_DHT11) != 0u) ? "" : "INVALID,",
                      (unsigned int)(humidity / 100u),
                      (unsigned int)(humidity % 100u),
                      (unsigned int)s_sample.ntc_adc_raw,
                      (unsigned int)s_sample.ntc_millivolts,
                      (unsigned int)s_sample.ntc_digital_level,
                      ((s_sample.valid_flags & EG_VALID_ULTRASONIC) != 0u) ? "" : "INVALID,",
                      (unsigned int)s_sample.distance_mm,
                      (unsigned int)s_sample.valid_flags,
                      (unsigned long)s_dht_errors,
                      (unsigned long)s_ntc_errors,
                      (unsigned long)s_ultrasonic_errors);
    if (length > 0) {
        size_t send_length;

        send_length = (size_t)length;
        if (send_length >= sizeof(line)) {
            send_length = sizeof(line) - 1u;
        }
        (void)HAL_UART_Transmit(&huart1, (uint8_t *)line,
                               (uint16_t)send_length, 50u);
    }
}

static void send_sensor_frame(void)
{
    uint8_t payload[EG_SENSOR_PAYLOAD_SIZE];
    uint8_t frame[EG_MAX_FRAME_SIZE];
    size_t frame_length;

    EG_Protocol_EncodeSensor(&s_sample, payload);
    frame_length = EG_Protocol_BuildFrame(EG_MSG_SENSOR, s_sequence,
                                          payload, sizeof(payload),
                                          frame, sizeof(frame));
    if (frame_length > 0u) {
        (void)HAL_UART_Transmit(&huart2, frame, (uint16_t)frame_length, 50u);
    }
}

HAL_StatusTypeDef EdgeGateway_Init(void)
{
    HAL_StatusTypeDef status;
    uint32_t now;

    memset(&s_sample, 0, sizeof(s_sample));
    status = EG_Time_Init();
    if (status != HAL_OK) {
        return status;
    }
    status = EG_NTC_Init();
    if (status != HAL_OK) {
        return status;
    }
    EG_Ultrasonic_Init();

    now = HAL_GetTick();
    s_next_ntc_ms = now;
    s_next_ultrasonic_ms = now + 100u;
    /* DHT11 needs about one second after power-up before the first request. */
    s_next_dht11_ms = now + 1200u;
    s_next_publish_ms = now + EG_PUBLISH_PERIOD_MS;
    return HAL_OK;
}

void EdgeGateway_RunOnce(void)
{
    uint32_t now;
    EG_NTC_Sample ntc;
    EG_DHT11_Sample dht;
    uint16_t distance_mm;

    now = HAL_GetTick();
    EG_Ultrasonic_Poll(now);

    if (EG_Ultrasonic_TakeResult(&distance_mm)) {
        s_sample.distance_mm = distance_mm;
        s_sample.valid_flags |= EG_VALID_ULTRASONIC;
    }
    if (EG_Ultrasonic_TimedOut()) {
        s_sample.valid_flags &= (uint8_t)~EG_VALID_ULTRASONIC;
        ++s_ultrasonic_errors;
    }

    if (time_reached(now, s_next_ntc_ms)) {
        HAL_StatusTypeDef ntc_status;

        s_next_ntc_ms = now + EG_NTC_PERIOD_MS;
        ntc_status = EG_NTC_Read(&ntc);
        s_sample.ntc_digital_level = ntc.digital_level;
        s_sample.valid_flags |= EG_VALID_NTC_DIGITAL;
        if (ntc_status == HAL_OK) {
            s_sample.ntc_adc_raw = ntc.adc_raw;
            s_sample.ntc_millivolts = ntc.millivolts;
            s_sample.valid_flags |= EG_VALID_NTC_ANALOG;
        } else {
            s_sample.valid_flags &= (uint8_t)~EG_VALID_NTC_ANALOG;
            ++s_ntc_errors;
        }
    }

    if (time_reached(now, s_next_dht11_ms) && !EG_Ultrasonic_IsBusy()) {
        s_next_dht11_ms = now + EG_DHT11_PERIOD_MS;
        if (EG_DHT11_Read(&dht) == EG_DHT11_OK) {
            s_sample.temperature_centi_c = dht.temperature_centi_c;
            s_sample.humidity_centi_percent = dht.humidity_centi_percent;
            s_sample.valid_flags |= EG_VALID_DHT11;
        } else {
            s_sample.valid_flags &= (uint8_t)~EG_VALID_DHT11;
            ++s_dht_errors;
        }
        now = HAL_GetTick();
    }

    if (time_reached(now, s_next_ultrasonic_ms) &&
        !EG_Ultrasonic_IsBusy()) {
        s_next_ultrasonic_ms = now + EG_ULTRASONIC_PERIOD_MS;
        if (EG_Ultrasonic_Start(now) != HAL_OK) {
            s_sample.valid_flags &= (uint8_t)~EG_VALID_ULTRASONIC;
            ++s_ultrasonic_errors;
        }
    }

    if (time_reached(now, s_next_publish_ms)) {
        s_next_publish_ms = now + EG_PUBLISH_PERIOD_MS;
        ++s_sequence;
        send_sensor_frame();
        send_debug_line();
    }
}
