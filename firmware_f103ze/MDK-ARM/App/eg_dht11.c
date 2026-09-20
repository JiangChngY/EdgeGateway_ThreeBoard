#include "eg_dht11.h"

#include "eg_time.h"
#include "main.h"

static GPIO_PinState dht_read_pin(void)
{
    return HAL_GPIO_ReadPin(DHT11_DATA_GPIO_Port, DHT11_DATA_Pin);
}

static uint8_t wait_while_level(GPIO_PinState level, uint16_t timeout_us)
{
    uint16_t started;

    started = EG_Time_Micros16();
    while (dht_read_pin() == level) {
        if ((uint16_t)(EG_Time_Micros16() - started) >= timeout_us) {
            return 0u;
        }
    }
    return 1u;
}

EG_DHT11_Status EG_DHT11_Read(EG_DHT11_Sample *sample)
{
    uint8_t data[5] = {0u, 0u, 0u, 0u, 0u};
    uint8_t bit_index;
    uint8_t byte_index;
    uint16_t high_started;
    uint16_t high_width;
    int16_t temperature_centi;
    uint16_t humidity_centi;

    if (sample == NULL) {
        return EG_DHT11_RANGE;
    }

    /* Open-drain low starts the request; writing SET releases the bus. */
    HAL_GPIO_WritePin(DHT11_DATA_GPIO_Port, DHT11_DATA_Pin, GPIO_PIN_RESET);
    HAL_Delay(18u);
    HAL_GPIO_WritePin(DHT11_DATA_GPIO_Port, DHT11_DATA_Pin, GPIO_PIN_SET);
    EG_Time_DelayUs(30u);

    /* Sensor response: about 80 us low, 80 us high, then first-bit low. */
    if (!wait_while_level(GPIO_PIN_SET, 120u) ||
        !wait_while_level(GPIO_PIN_RESET, 120u) ||
        !wait_while_level(GPIO_PIN_SET, 120u)) {
        return EG_DHT11_TIMEOUT;
    }

    for (bit_index = 0u; bit_index < 40u; ++bit_index) {
        if (!wait_while_level(GPIO_PIN_RESET, 100u)) {
            return EG_DHT11_TIMEOUT;
        }

        high_started = EG_Time_Micros16();
        if (!wait_while_level(GPIO_PIN_SET, 120u)) {
            return EG_DHT11_TIMEOUT;
        }
        high_width = (uint16_t)(EG_Time_Micros16() - high_started);

        byte_index = (uint8_t)(bit_index >> 3);
        data[byte_index] <<= 1;
        if (high_width > 50u) {
            data[byte_index] |= 1u;
        }
    }

    if ((uint8_t)(data[0] + data[1] + data[2] + data[3]) != data[4]) {
        return EG_DHT11_CHECKSUM;
    }

    humidity_centi = (uint16_t)((uint16_t)data[0] * 100u + (uint16_t)data[1] * 10u);
    temperature_centi = (int16_t)((int16_t)(data[2] & 0x7Fu) * 100 +
                                  (int16_t)data[3] * 10);
    if ((data[2] & 0x80u) != 0u) {
        temperature_centi = (int16_t)-temperature_centi;
    }

    if (humidity_centi > 10000u || temperature_centi < -4000 ||
        temperature_centi > 8000) {
        return EG_DHT11_RANGE;
    }

    sample->temperature_centi_c = temperature_centi;
    sample->humidity_centi_percent = humidity_centi;
    return EG_DHT11_OK;
}
