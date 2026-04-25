#include "cluster_crc32.h"
#include <stddef.h>

#define CLUSTER_CRC32_POLYNOMIAL 0xEDB88320UL

uint32_t cluster_crc32_seed(void) {
    return 0xFFFFFFFFUL;
}

uint32_t cluster_crc32_update(uint32_t crc, const void *data, uint32_t length) {
    const uint8_t *bytes;
    uint32_t index;
    uint8_t bit;

    if (data == NULL || length == 0U) {
        return crc;
    }

    bytes = (const uint8_t *)data;
    for (index = 0U; index < length; index += 1U) {
        crc ^= bytes[index];
        for (bit = 0U; bit < 8U; bit += 1U) {
            if ((crc & 1UL) != 0UL) {
                crc = (crc >> 1U) ^ CLUSTER_CRC32_POLYNOMIAL;
            } else {
                crc >>= 1U;
            }
        }
    }

    return crc;
}

uint32_t cluster_crc32_finalize(uint32_t crc) {
    return crc ^ 0xFFFFFFFFUL;
}

uint32_t cluster_crc32_compute(const void *data, uint32_t length) {
    return cluster_crc32_finalize(cluster_crc32_update(cluster_crc32_seed(),
                                                       data,
                                                       length));
}
