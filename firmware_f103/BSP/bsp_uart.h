#ifndef BSP_UART_H
#define BSP_UART_H

#include <stddef.h>
#include <stdint.h>

void BSP_UART_Init(uint32_t baudrate);
void BSP_UART_Write(const uint8_t *data, size_t length);
int BSP_UART_ReadByte(uint8_t *value);
uint32_t BSP_UART_RxOverflowCount(void);

#endif

