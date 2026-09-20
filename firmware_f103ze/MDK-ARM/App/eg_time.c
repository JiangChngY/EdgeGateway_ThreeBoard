#include "eg_time.h"

#include "tim.h"

HAL_StatusTypeDef EG_Time_Init(void)
{
    __HAL_TIM_SET_COUNTER(&htim6, 0u);
    return HAL_TIM_Base_Start(&htim6);
}

uint16_t EG_Time_Micros16(void)
{
    return (uint16_t)__HAL_TIM_GET_COUNTER(&htim6);
}

void EG_Time_DelayUs(uint16_t microseconds)
{
    uint16_t started;

    started = EG_Time_Micros16();
    while ((uint16_t)(EG_Time_Micros16() - started) < microseconds) {
        /* TIM6 runs freely at 1 MHz. */
    }
}
