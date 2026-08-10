#include "bsp_watchdog.h"

#include "stm32f10x.h"

void BSP_Watchdog_Init(void)
{
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(IWDG_Prescaler_64);
    IWDG_SetReload(1250u);
    IWDG_ReloadCounter();
    IWDG_Enable();
}

void BSP_Watchdog_Feed(void)
{
    IWDG_ReloadCounter();
}

