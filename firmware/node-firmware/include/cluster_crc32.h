#ifndef CLUSTER_CRC32_H
#define CLUSTER_CRC32_H

#include <stdint.h>

uint32_t cluster_crc32_seed(void);
uint32_t cluster_crc32_update(uint32_t crc, const void *data, uint32_t length);
uint32_t cluster_crc32_finalize(uint32_t crc);
uint32_t cluster_crc32_compute(const void *data, uint32_t length);

#endif
