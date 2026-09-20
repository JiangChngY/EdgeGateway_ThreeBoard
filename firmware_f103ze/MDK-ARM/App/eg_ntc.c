#include "eg_ntc.h"

#include "adc.h"
#include "main.h"

#define EG_NTC_OVERSAMPLE_COUNT 16u
#define EG_ADC_REFERENCE_MV     3300u
#define EG_ADC_FULL_SCALE       4095u

HAL_StatusTypeDef EG_NTC_Init(void)
{
    return HAL_ADCEx_Calibration_Start(&hadc1);
}

HAL_StatusTypeDef EG_NTC_Read(EG_NTC_Sample *sample)
{
    uint32_t sum;
    uint32_t index;
    uint16_t average;

    if (sample == NULL) {
        return HAL_ERROR;
    }

    sample->digital_level = (HAL_GPIO_ReadPin(NTC_DO_GPIO_Port, NTC_DO_Pin) ==
                             GPIO_PIN_SET) ? 1u : 0u;
    sum = 0u;
    for (index = 0u; index < EG_NTC_OVERSAMPLE_COUNT; ++index) {
        if (HAL_ADC_Start(&hadc1) != HAL_OK) {
            return HAL_ERROR;
        }
        if (HAL_ADC_PollForConversion(&hadc1, 5u) != HAL_OK) {
            (void)HAL_ADC_Stop(&hadc1);
            return HAL_TIMEOUT;
        }
        sum += HAL_ADC_GetValue(&hadc1);
        if (HAL_ADC_Stop(&hadc1) != HAL_OK) {
            return HAL_ERROR;
        }
    }

    average = (uint16_t)((sum + (EG_NTC_OVERSAMPLE_COUNT / 2u)) /
                         EG_NTC_OVERSAMPLE_COUNT);
    sample->adc_raw = average;
    sample->millivolts = (uint16_t)(((uint32_t)average * EG_ADC_REFERENCE_MV +
                                     (EG_ADC_FULL_SCALE / 2u)) /
                                    EG_ADC_FULL_SCALE);
    return HAL_OK;
}
