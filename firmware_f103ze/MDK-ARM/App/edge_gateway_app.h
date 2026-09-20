#ifndef EDGE_GATEWAY_APP_H
#define EDGE_GATEWAY_APP_H

#include "stm32f1xx_hal.h"

HAL_StatusTypeDef EdgeGateway_Init(void);
void EdgeGateway_RunOnce(void);

#endif
