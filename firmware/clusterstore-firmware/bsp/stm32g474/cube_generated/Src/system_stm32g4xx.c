#include "main.h"

uint32_t SystemCoreClock = 80000000U;
const uint8_t AHBPrescTable[16U] = {0U, 0U, 0U, 0U, 1U, 2U, 3U, 4U,
                                    6U, 7U, 8U, 9U, 0U, 0U, 0U, 0U};
const uint8_t APBPrescTable[8U] = {0U, 0U, 0U, 0U, 1U, 2U, 3U, 4U};

void SystemInit(void) {
}

void SystemCoreClockUpdate(void) {
    SystemCoreClock = 80000000U;
}
