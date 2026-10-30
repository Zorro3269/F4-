#include "main.h"

extern CAN_HandleTypeDef hcan1;
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart3;

void CAN1_RX0_IRQHandler(void) { HAL_CAN_IRQHandler(&hcan1); }
void USART1_IRQHandler(void) { HAL_UART_IRQHandler(&huart1); }
void USART3_IRQHandler(void) { HAL_UART_IRQHandler(&huart3); }
