#ifndef CS_IWDG_G474_H
#define CS_IWDG_G474_H

#include <stdint.h>
#include "cs_cluster_platform.h"

typedef struct {
    void *hiwdg;
    uint32_t prescaler;
    uint32_t reload;
    uint32_t window;
    uint8_t auto_start;
} cs_g474_iwdg_config_t;

typedef struct {
    cs_g474_iwdg_config_t config;
    uint8_t started;
} cs_g474_iwdg_t;

cs_status_t cs_iwdg_g474_init(cs_g474_iwdg_t *watchdog,
                              const cs_g474_iwdg_config_t *config);
cs_status_t cs_iwdg_g474_start(cs_g474_iwdg_t *watchdog);
cs_status_t cs_iwdg_g474_kick(cs_g474_iwdg_t *watchdog);

#endif
