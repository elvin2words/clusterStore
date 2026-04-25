#ifndef CLUSTER_CURRENT_RAMP_H
#define CLUSTER_CURRENT_RAMP_H

#include <stdint.h>

typedef struct {
    int16_t current_da;
    uint16_t ramp_up_da_per_s;
    uint16_t ramp_down_da_per_s;
} cluster_current_ramp_t;

void cluster_current_ramp_init(cluster_current_ramp_t *ramp,
                               uint16_t ramp_up_da_per_s,
                               uint16_t ramp_down_da_per_s);
int16_t cluster_current_ramp_step(cluster_current_ramp_t *ramp,
                                  int16_t target_da,
                                  uint16_t delta_ms);

#endif

