#ifndef CLUSTER_STM32_BOOT_H
#define CLUSTER_STM32_BOOT_H

#include <stdint.h>

void cluster_stm32_jump_to_image(void *context, uint32_t vector_table_address);

#endif
