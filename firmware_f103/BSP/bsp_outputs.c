#include "bsp_outputs.h"

#include "stm32f10x.h"

void BSP_Outputs_Init(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    gpio.GPIO_Pin = GPIO_Pin_13;
    GPIO_Init(GPIOC, &gpio);
    GPIO_SetBits(GPIOC, GPIO_Pin_13);

    gpio.GPIO_Pin = GPIO_Pin_13;
    GPIO_Init(GPIOB, &gpio);
    GPIO_ResetBits(GPIOB, GPIO_Pin_13);
}

void BSP_LED_Set(int enabled)
{
    if (enabled) {
        GPIO_ResetBits(GPIOC, GPIO_Pin_13);
    } else {
        GPIO_SetBits(GPIOC, GPIO_Pin_13);
    }
}

void BSP_Buzzer_Set(int enabled)
{
    if (enabled) {
        GPIO_SetBits(GPIOB, GPIO_Pin_13);
    } else {
        GPIO_ResetBits(GPIOB, GPIO_Pin_13);
    }
}

