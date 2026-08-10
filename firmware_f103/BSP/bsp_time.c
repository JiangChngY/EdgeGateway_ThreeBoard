#include "bsp_time.h"

#include "stm32f10x.h"

static volatile uint32_t s_millis;

void BSP_Time_Init(void)
{
    TIM_TimeBaseInitTypeDef timer;

    SysTick_Config(SystemCoreClock / 1000u);

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);
    TIM_TimeBaseStructInit(&timer);
    timer.TIM_Prescaler = (uint16_t)(SystemCoreClock / 1000000u - 1u);
    timer.TIM_Period = 0xFFFFu;
    timer.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM4, &timer);
    TIM_Cmd(TIM4, ENABLE);
}

uint32_t BSP_Time_Millis(void)
{
    return s_millis;
}

uint16_t BSP_Time_Micros16(void)
{
    return (uint16_t)TIM_GetCounter(TIM4);
}

void BSP_Time_DelayUs(uint16_t microseconds)
{
    uint16_t start = BSP_Time_Micros16();
    while ((uint16_t)(BSP_Time_Micros16() - start) < microseconds) {
    }
}

void BSP_Time_DelayMs(uint32_t milliseconds)
{
    uint32_t start = BSP_Time_Millis();
    while ((uint32_t)(BSP_Time_Millis() - start) < milliseconds) {
    }
}

void BSP_Time_SysTickHandler(void)
{
    ++s_millis;
}
