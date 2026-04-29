#include "cluster_stm32_boot.h"

#ifdef CLUSTER_STM32_HAL_HEADER
#include CLUSTER_STM32_HAL_HEADER
#endif

#ifdef CLUSTER_STM32_CMSIS_HEADER
#include CLUSTER_STM32_CMSIS_HEADER
#endif

typedef void (*cluster_stm32_entry_fn)(void);

void cluster_stm32_jump_to_image(void *context, uint32_t vector_table_address) {
    uint32_t stack_pointer;
    uint32_t reset_handler;
    cluster_stm32_entry_fn entry;

    (void)context;
    if (vector_table_address == 0U) {
        return;
    }

    stack_pointer = *((uint32_t *)vector_table_address);
    reset_handler = *((uint32_t *)(vector_table_address + 4U));
    entry = (cluster_stm32_entry_fn)reset_handler;

#ifdef CLUSTER_STM32_CMSIS_HEADER
    __disable_irq();
#endif

#ifdef CLUSTER_STM32_HAL_HEADER
    HAL_DeInit();
#endif

#ifdef CLUSTER_STM32_CMSIS_HEADER
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;
#endif

#if defined(CLUSTER_STM32_CMSIS_HEADER) && defined(SCB_VTOR_TBLOFF_Msk)
    SCB->VTOR = vector_table_address;
#endif

#ifdef CLUSTER_STM32_CMSIS_HEADER
    __set_MSP(stack_pointer);
#endif

    entry();
}
