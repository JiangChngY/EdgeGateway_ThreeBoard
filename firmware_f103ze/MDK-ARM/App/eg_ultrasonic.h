#ifndef EG_ULTRASONIC_H
#define EG_ULTRASONIC_H

#include "stm32f1xx_hal.h"

#include <stdint.h>

typedef enum {
    EG_ULTRASONIC_IDLE = 0,
    EG_ULTRASONIC_WAIT_RISE,
    EG_ULTRASONIC_WAIT_FALL,
    EG_ULTRASONIC_READY
} EG_Ultrasonic_State;

void EG_Ultrasonic_Init(void);
HAL_StatusTypeDef EG_Ultrasonic_Start(uint32_t now_ms);
void EG_Ultrasonic_Poll(uint32_t now_ms);
uint8_t EG_Ultrasonic_IsBusy(void);
uint8_t EG_Ultrasonic_TakeResult(uint16_t *distance_mm);
uint8_t EG_Ultrasonic_TimedOut(void);

#endif
