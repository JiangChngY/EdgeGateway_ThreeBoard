#ifndef BSP_TIME_H
#define BSP_TIME_H

#include <stdint.h>

void BSP_Time_Init(void);
uint32_t BSP_Time_Millis(void);
uint16_t BSP_Time_Micros16(void);
void BSP_Time_DelayUs(uint16_t microseconds);
void BSP_Time_DelayMs(uint32_t milliseconds);
void BSP_Time_SysTickHandler(void);

#endif
