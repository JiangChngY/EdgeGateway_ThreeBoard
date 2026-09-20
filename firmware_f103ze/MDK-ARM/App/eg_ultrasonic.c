#include "eg_ultrasonic.h"

#include "eg_time.h"
#include "main.h"
#include "tim.h"

#define EG_ULTRASONIC_TIMEOUT_MS   40u
#define EG_ULTRASONIC_MIN_PULSE_US 116u
#define EG_ULTRASONIC_MAX_PULSE_US 26240u

static volatile EG_Ultrasonic_State s_state = EG_ULTRASONIC_IDLE;
static volatile uint16_t s_rising_capture = 0u;
static volatile uint16_t s_pulse_width_us = 0u;
static volatile uint8_t s_timed_out = 0u;
static uint32_t s_started_ms = 0u;

void EG_Ultrasonic_Init(void)
{
    HAL_GPIO_WritePin(US_TRIG_GPIO_Port, US_TRIG_Pin, GPIO_PIN_RESET);
    s_state = EG_ULTRASONIC_IDLE;
    s_timed_out = 0u;
    __HAL_TIM_SET_CAPTUREPOLARITY(&htim4, TIM_CHANNEL_1,
                                  TIM_INPUTCHANNELPOLARITY_RISING);
}

HAL_StatusTypeDef EG_Ultrasonic_Start(uint32_t now_ms)
{
    HAL_StatusTypeDef status;

    if (s_state != EG_ULTRASONIC_IDLE) {
        return HAL_BUSY;
    }

    s_timed_out = 0u;
    s_pulse_width_us = 0u;
    s_started_ms = now_ms;
    s_state = EG_ULTRASONIC_WAIT_RISE;
    __HAL_TIM_SET_CAPTUREPOLARITY(&htim4, TIM_CHANNEL_1,
                                  TIM_INPUTCHANNELPOLARITY_RISING);
    __HAL_TIM_CLEAR_FLAG(&htim4, TIM_FLAG_CC1);
    status = HAL_TIM_IC_Start_IT(&htim4, TIM_CHANNEL_1);
    if (status != HAL_OK) {
        s_state = EG_ULTRASONIC_IDLE;
        return status;
    }

    HAL_GPIO_WritePin(US_TRIG_GPIO_Port, US_TRIG_Pin, GPIO_PIN_RESET);
    EG_Time_DelayUs(2u);
    HAL_GPIO_WritePin(US_TRIG_GPIO_Port, US_TRIG_Pin, GPIO_PIN_SET);
    EG_Time_DelayUs(10u);
    HAL_GPIO_WritePin(US_TRIG_GPIO_Port, US_TRIG_Pin, GPIO_PIN_RESET);
    return HAL_OK;
}

void EG_Ultrasonic_Poll(uint32_t now_ms)
{
    if ((s_state == EG_ULTRASONIC_WAIT_RISE ||
         s_state == EG_ULTRASONIC_WAIT_FALL) &&
        (uint32_t)(now_ms - s_started_ms) >= EG_ULTRASONIC_TIMEOUT_MS) {
        (void)HAL_TIM_IC_Stop_IT(&htim4, TIM_CHANNEL_1);
        __HAL_TIM_SET_CAPTUREPOLARITY(&htim4, TIM_CHANNEL_1,
                                      TIM_INPUTCHANNELPOLARITY_RISING);
        s_state = EG_ULTRASONIC_IDLE;
        s_timed_out = 1u;
    }
}

uint8_t EG_Ultrasonic_IsBusy(void)
{
    return (s_state == EG_ULTRASONIC_WAIT_RISE ||
            s_state == EG_ULTRASONIC_WAIT_FALL) ? 1u : 0u;
}

uint8_t EG_Ultrasonic_TakeResult(uint16_t *distance_mm)
{
    uint16_t pulse;

    if (distance_mm == NULL || s_state != EG_ULTRASONIC_READY) {
        return 0u;
    }

    pulse = s_pulse_width_us;
    s_state = EG_ULTRASONIC_IDLE;
    if (pulse < EG_ULTRASONIC_MIN_PULSE_US ||
        pulse > EG_ULTRASONIC_MAX_PULSE_US) {
        s_timed_out = 1u;
        return 0u;
    }

    /* distance = echo_time * 343 m/s / 2, rounded to millimetres. */
    *distance_mm = (uint16_t)(((uint32_t)pulse * 343u + 1000u) / 2000u);
    return 1u;
}

uint8_t EG_Ultrasonic_TimedOut(void)
{
    uint8_t result;

    result = s_timed_out;
    s_timed_out = 0u;
    return result;
}

void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    uint16_t capture;

    if (htim == NULL || htim->Instance != TIM4 ||
        htim->Channel != HAL_TIM_ACTIVE_CHANNEL_1) {
        return;
    }

    capture = (uint16_t)HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);
    if (s_state == EG_ULTRASONIC_WAIT_RISE) {
        s_rising_capture = capture;
        s_state = EG_ULTRASONIC_WAIT_FALL;
        __HAL_TIM_SET_CAPTUREPOLARITY(htim, TIM_CHANNEL_1,
                                      TIM_INPUTCHANNELPOLARITY_FALLING);
    } else if (s_state == EG_ULTRASONIC_WAIT_FALL) {
        s_pulse_width_us = (uint16_t)(capture - s_rising_capture);
        (void)HAL_TIM_IC_Stop_IT(htim, TIM_CHANNEL_1);
        __HAL_TIM_SET_CAPTUREPOLARITY(htim, TIM_CHANNEL_1,
                                      TIM_INPUTCHANNELPOLARITY_RISING);
        s_state = EG_ULTRASONIC_READY;
    }
}
