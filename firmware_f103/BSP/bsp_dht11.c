#include "bsp_dht11.h"

#include "bsp_time.h"
#include "stm32f10x.h"

#define DHT_PORT GPIOB
#define DHT_PIN GPIO_Pin_12

static void pin_output(void)
{
    GPIO_InitTypeDef gpio;
    gpio.GPIO_Pin = DHT_PIN;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    gpio.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_Init(DHT_PORT, &gpio);
}

static void pin_input(void)
{
    GPIO_InitTypeDef gpio;
    gpio.GPIO_Pin = DHT_PIN;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(DHT_PORT, &gpio);
}

static int wait_while_level(BitAction level, uint16_t timeout_us)
{
    uint16_t start = BSP_Time_Micros16();
    while (GPIO_ReadInputDataBit(DHT_PORT, DHT_PIN) == level) {
        if ((uint16_t)(BSP_Time_Micros16() - start) >= timeout_us) {
            return 0;
        }
    }
    return 1;
}

void BSP_DHT11_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    pin_output();
    GPIO_SetBits(DHT_PORT, DHT_PIN);
}

int BSP_DHT11_Read(int16_t *temperature_centi_c,
                   uint16_t *humidity_centi_percent)
{
    uint8_t data[5] = {0u, 0u, 0u, 0u, 0u};
    uint8_t byte_index;
    uint8_t bit_index;
    uint16_t high_started;
    uint16_t high_width;

    if (temperature_centi_c == 0 || humidity_centi_percent == 0) {
        return 0;
    }

    pin_output();
    GPIO_ResetBits(DHT_PORT, DHT_PIN);
    BSP_Time_DelayMs(20u);
    GPIO_SetBits(DHT_PORT, DHT_PIN);
    BSP_Time_DelayUs(30u);
    pin_input();

    /* DHT11 response: idle-high -> 80us low -> 80us high -> first data low. */
    if (!wait_while_level(Bit_SET, 100u) ||
        !wait_while_level(Bit_RESET, 100u) ||
        !wait_while_level(Bit_SET, 100u)) {
        return 0;
    }

    for (byte_index = 0u; byte_index < 5u; ++byte_index) {
        for (bit_index = 0u; bit_index < 8u; ++bit_index) {
            /* Every bit starts with about 50us low. Its high pulse is
             * about 26-28us for 0 and about 70us for 1. */
            if (!wait_while_level(Bit_RESET, 80u)) {
                return 0;
            }
            high_started = BSP_Time_Micros16();
            if (!wait_while_level(Bit_SET, 100u)) {
                return 0;
            }
            high_width = (uint16_t)(BSP_Time_Micros16() - high_started);
            data[byte_index] <<= 1;
            if (high_width >= 45u) {
                data[byte_index] |= 1u;
            }
        }
    }

    if ((uint8_t)(data[0] + data[1] + data[2] + data[3]) != data[4]) {
        return 0;
    }

    *humidity_centi_percent = (uint16_t)((uint16_t)data[0] * 100u + data[1]);
    *temperature_centi_c = (int16_t)((int16_t)data[2] * 100 + data[3]);
    return 1;
}
