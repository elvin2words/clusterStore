#include "cs_flash_g474.h"
#include <stddef.h>
#include <string.h>

#ifdef CS_G474_USE_HAL
#include "stm32g4xx_hal.h"
#endif

static bool cs_flash_g474_has_host_storage(const cs_g474_flash_t *flash) {
    return flash != NULL && flash->config.host_flash_bytes != NULL;
}

static cs_status_t cs_flash_g474_validate_range(const cs_g474_flash_t *flash,
                                                uint32_t address,
                                                uint32_t length) {
    if (flash == NULL || length == 0U || address < flash->config.flash_base_address ||
        length > flash->config.flash_size_bytes ||
        (address - flash->config.flash_base_address) >
            (flash->config.flash_size_bytes - length)) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    return CS_STATUS_OK;
}

#ifdef CS_G474_USE_HAL
static uint8_t cs_flash_g474_bank_index(const cs_g474_flash_t *flash,
                                        uint32_t address) {
    return (uint8_t)((address - flash->config.flash_base_address) /
                     flash->config.bank_size_bytes);
}

static uint32_t cs_flash_g474_bank_constant(uint8_t bank_index) {
    return bank_index == 0U ? FLASH_BANK_1 : FLASH_BANK_2;
}

static uint32_t cs_flash_g474_page_index_in_bank(const cs_g474_flash_t *flash,
                                                 uint32_t address) {
    uint32_t bank_base;
    uint8_t bank_index;

    bank_index = cs_flash_g474_bank_index(flash, address);
    bank_base = flash->config.flash_base_address +
                ((uint32_t)bank_index * flash->config.bank_size_bytes);
    return (address - bank_base) / flash->config.page_size_bytes;
}
#endif

bool cs_flash_g474_address_to_bank_page(const cs_g474_flash_t *flash,
                                        uint32_t address,
                                        uint8_t *bank_index_out,
                                        uint32_t *page_index_out) {
    uint32_t offset;
    uint8_t bank_index;
    uint32_t bank_offset;

    if (flash == NULL || flash->config.bank_size_bytes == 0U ||
        flash->config.page_size_bytes == 0U ||
        cs_flash_g474_validate_range(flash, address, 1U) != CS_STATUS_OK) {
        return false;
    }

    offset = address - flash->config.flash_base_address;
    bank_index = (uint8_t)(offset / flash->config.bank_size_bytes);
    if (bank_index >= flash->config.bank_count) {
        return false;
    }

    bank_offset = offset - ((uint32_t)bank_index * flash->config.bank_size_bytes);
    if (bank_index_out != NULL) {
        *bank_index_out = bank_index;
    }
    if (page_index_out != NULL) {
        *page_index_out = bank_offset / flash->config.page_size_bytes;
    }
    return true;
}

cs_status_t cs_flash_g474_init(cs_g474_flash_t *flash,
                               const cs_g474_flash_config_t *config) {
    if (flash == NULL) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    memset(flash, 0, sizeof(*flash));
    if (config != NULL) {
        flash->config = *config;
    }

    if (flash->config.flash_base_address == 0U) {
        flash->config.flash_base_address = CS_G474_FLASH_BASE_ADDRESS;
    }
    if (flash->config.flash_size_bytes == 0U) {
        flash->config.flash_size_bytes = CS_G474_FLASH_SIZE_BYTES;
    }
    if (flash->config.page_size_bytes == 0U) {
        flash->config.page_size_bytes = CS_G474_FLASH_PAGE_SIZE_BYTES;
    }
    if (flash->config.bank_size_bytes == 0U) {
        flash->config.bank_size_bytes = CS_G474_FLASH_BANK_SIZE_BYTES;
    }
    if (flash->config.bank_count == 0U) {
        flash->config.bank_count = CS_G474_FLASH_BANK_COUNT;
    }

    if (flash->config.flash_base_address == 0U ||
        flash->config.flash_size_bytes == 0U ||
        flash->config.page_size_bytes == 0U ||
        flash->config.bank_size_bytes == 0U ||
        flash->config.bank_count == 0U) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    return CS_STATUS_OK;
}

cs_status_t cs_flash_g474_read(void *context,
                               uint32_t address,
                               void *buffer,
                               uint32_t length) {
    cs_g474_flash_t *flash;

    flash = (cs_g474_flash_t *)context;
    if (buffer == NULL) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    if (cs_flash_g474_validate_range(flash, address, length) != CS_STATUS_OK) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    if (cs_flash_g474_has_host_storage(flash)) {
        memcpy(buffer,
               flash->config.host_flash_bytes +
                   (address - flash->config.flash_base_address),
               length);
        return CS_STATUS_OK;
    }

#ifdef CS_G474_USE_HAL
    memcpy(buffer, (const void *)(uintptr_t)address, length);
    return CS_STATUS_OK;
#else
    (void)address;
    (void)length;
    return CS_STATUS_UNSUPPORTED;
#endif
}

cs_status_t cs_flash_g474_write(void *context,
                                uint32_t address,
                                const void *data,
                                uint32_t length) {
    cs_g474_flash_t *flash;

    flash = (cs_g474_flash_t *)context;
    if (data == NULL ||
        cs_flash_g474_validate_range(flash, address, length) != CS_STATUS_OK ||
        (address % 8U) != 0U || (length % 8U) != 0U) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    if (cs_flash_g474_has_host_storage(flash)) {
        const uint8_t *bytes;
        uint8_t *destination;
        uint32_t offset;

        bytes = (const uint8_t *)data;
        destination =
            flash->config.host_flash_bytes + (address - flash->config.flash_base_address);
        for (offset = 0U; offset < length; offset += 1U) {
            if (destination[offset] != 0xFFU) {
                return CS_STATUS_ERROR;
            }
        }

        memcpy(destination, bytes, length);
        return CS_STATUS_OK;
    }

#ifdef CS_G474_USE_HAL
    {
        const uint8_t *bytes;
        uint32_t offset;

        bytes = (const uint8_t *)data;
        if (HAL_FLASH_Unlock() != HAL_OK) {
            return CS_STATUS_ERROR;
        }

        for (offset = 0U; offset < length; offset += 8U) {
            uint64_t word;

            memcpy(&word, &bytes[offset], sizeof(word));
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
                                  address + offset,
                                  word) != HAL_OK) {
                HAL_FLASH_Lock();
                return CS_STATUS_ERROR;
            }
        }

        HAL_FLASH_Lock();
        return CS_STATUS_OK;
    }
#else
    (void)address;
    (void)length;
    return CS_STATUS_UNSUPPORTED;
#endif
}

cs_status_t cs_flash_g474_erase(void *context,
                                uint32_t address,
                                uint32_t length) {
    cs_g474_flash_t *flash;

    flash = (cs_g474_flash_t *)context;
    if (cs_flash_g474_validate_range(flash, address, length) != CS_STATUS_OK ||
        (address % flash->config.page_size_bytes) != 0U ||
        (length % flash->config.page_size_bytes) != 0U) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    if (cs_flash_g474_has_host_storage(flash)) {
        memset(flash->config.host_flash_bytes +
                   (address - flash->config.flash_base_address),
               0xFF,
               length);
        return CS_STATUS_OK;
    }

#ifdef CS_G474_USE_HAL
    {
        uint32_t remaining_bytes;
        uint32_t current_address;

        if (HAL_FLASH_Unlock() != HAL_OK) {
            return CS_STATUS_ERROR;
        }

        remaining_bytes = length;
        current_address = address;
        while (remaining_bytes > 0U) {
            FLASH_EraseInitTypeDef erase;
            uint32_t page_error;
            uint8_t bank_index;
            uint32_t bank_base;
            uint32_t pages_available_in_bank;
            uint32_t pages_requested;
            uint32_t pages_to_erase;

            bank_index = cs_flash_g474_bank_index(flash, current_address);
            bank_base = flash->config.flash_base_address +
                        ((uint32_t)bank_index * flash->config.bank_size_bytes);
            pages_available_in_bank =
                (flash->config.bank_size_bytes - (current_address - bank_base)) /
                flash->config.page_size_bytes;
            pages_requested = remaining_bytes / flash->config.page_size_bytes;
            pages_to_erase = pages_requested < pages_available_in_bank
                                 ? pages_requested
                                 : pages_available_in_bank;

            memset(&erase, 0, sizeof(erase));
            erase.TypeErase = FLASH_TYPEERASE_PAGES;
            erase.Banks = cs_flash_g474_bank_constant(bank_index);
            erase.Page = cs_flash_g474_page_index_in_bank(flash, current_address);
            erase.NbPages = pages_to_erase;

            if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK) {
                HAL_FLASH_Lock();
                return CS_STATUS_ERROR;
            }

            current_address += pages_to_erase * flash->config.page_size_bytes;
            remaining_bytes -= pages_to_erase * flash->config.page_size_bytes;
        }

        HAL_FLASH_Lock();
        return CS_STATUS_OK;
    }
#else
    (void)address;
    (void)length;
    return CS_STATUS_UNSUPPORTED;
#endif
}
