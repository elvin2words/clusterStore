#include "main.h"

uint32_t SystemCoreClock = 80000000U;

void SystemInit(void) {
}

void SystemCoreClockUpdate(void) {
    SystemCoreClock = 80000000U;
}
