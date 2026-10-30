#include "stm32f4xx.h"
#include <stdint.h>

extern int main(void);
extern uint32_t _estack, _sidata, _sdata, _edata, _sbss, _ebss;
void SystemInit(void);
void Reset_Handler(void);
void Default_Handler(void);
void SVC_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);
void CAN1_RX0_IRQHandler(void);
void USART1_IRQHandler(void);
void USART3_IRQHandler(void);

__attribute__((section(".isr_vector"), used))
const uintptr_t vector_table[16 + 82] = {
    [0] = (uintptr_t)&_estack,
    [1] = (uintptr_t)Reset_Handler,
    [2] = (uintptr_t)Default_Handler,
    [3] = (uintptr_t)Default_Handler,
    [4] = (uintptr_t)Default_Handler,
    [5] = (uintptr_t)Default_Handler,
    [6] = (uintptr_t)Default_Handler,
    [11] = (uintptr_t)SVC_Handler,
    [12] = (uintptr_t)Default_Handler,
    [14] = (uintptr_t)PendSV_Handler,
    [15] = (uintptr_t)SysTick_Handler,
    [16 + CAN1_RX0_IRQn] = (uintptr_t)CAN1_RX0_IRQHandler,
    [16 + USART1_IRQn] = (uintptr_t)USART1_IRQHandler,
    [16 + USART3_IRQn] = (uintptr_t)USART3_IRQHandler
};

void Reset_Handler(void)
{
    uint32_t *src = &_sidata;
    for (uint32_t *dst = &_sdata; dst < &_edata;) *dst++ = *src++;
    for (uint32_t *dst = &_sbss; dst < &_ebss;) *dst++ = 0;
    SystemInit();
    (void)main();
    for (;;) {}
}

void Default_Handler(void)
{
    for (;;) {}
}
