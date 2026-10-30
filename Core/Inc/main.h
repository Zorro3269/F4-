#ifndef MAIN_H
#define MAIN_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

typedef struct {
    int16_t value[6];
    uint8_t valid[6];
    uint8_t can_online;
    uint8_t wifi_online;
    uint8_t protocol;
    uint32_t updated_ms;
} VehicleData;

extern CAN_HandleTypeDef hcan1;
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart3;
extern volatile VehicleData vehicle_data;

#define ESP_EN_GPIO_PORT GPIOC
#define ESP_EN_GPIO_PIN GPIO_PIN_1
#define ESP_RST_GPIO_PORT GPIOF
#define ESP_RST_GPIO_PIN GPIO_PIN_6

void Error_Handler(void);

#endif
