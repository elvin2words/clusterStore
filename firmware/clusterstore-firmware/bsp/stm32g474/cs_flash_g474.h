#ifndef CS_FLASH_G474_H
#define CS_FLASH_G474_H

#include <stdint.h>
#include "cs_cluster_platform.h"

#define CS_G474_FLASH_BASE_ADDRESS 0x08000000UL
#define CS_G474_FLASH_SIZE_BYTES (512UL * 1024UL)
#define CS_G474_FLASH_PAGE_SIZE_BYTES 2048UL
#define CS_G474_FLASH_BANK_COUNT 2U
#define CS_G474_FLASH_BANK_SIZE_BYTES (256UL * 1024UL)

typedef struct {
    uint32_t flash_base_address;
    uint32_t flash_size_bytes;
    uint32_t page_size_bytes;
    uint32_t bank_size_bytes;
    uint8_t bank_count;
    uint8_t *host_flash_bytes;
} cs_g474_flash_config_t;

typedef struct {
    cs_g474_flash_config_t config;
} cs_g474_flash_t;

bool cs_flash_g474_address_to_bank_page(const cs_g474_flash_t *flash,
                                        uint32_t address,
                                        uint8_t *bank_index_out,
                                        uint32_t *page_index_out);
cs_status_t cs_flash_g474_init(cs_g474_flash_t *flash,
                               const cs_g474_flash_config_t *config);
cs_status_t cs_flash_g474_read(void *context,
                               uint32_t address,
                               void *buffer,
                               uint32_t length);
cs_status_t cs_flash_g474_write(void *context,
                                uint32_t address,
                                const void *data,
                                uint32_t length);
cs_status_t cs_flash_g474_erase(void *context,
                                uint32_t address,
                                uint32_t length);

#endif
