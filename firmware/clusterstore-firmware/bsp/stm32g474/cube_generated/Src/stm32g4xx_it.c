#include "cs_cube_g474_board.h"
#include "stm32g4xx_it.h"

extern FDCAN_HandleTypeDef hfdcan1;

void NMI_Handler(void) {
}

void HardFault_Handler(void) {
    Error_Handler();
}

void MemManage_Handler(void) {
    Error_Handler();
}

void BusFault_Handler(void) {
    Error_Handler();
}

void UsageFault_Handler(void) {
    Error_Handler();
}

void SVC_Handler(void) {
}

void DebugMon_Handler(void) {
}

void PendSV_Handler(void) {
}

void SysTick_Handler(void) {
    HAL_IncTick();
}

void FDCAN1_IT0_IRQHandler(void) {
    HAL_FDCAN_IRQHandler(&hfdcan1);
}
