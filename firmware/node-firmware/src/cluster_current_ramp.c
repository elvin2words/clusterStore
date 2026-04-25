#include "cluster_current_ramp.h"

static int16_t cluster_limit_step(int16_t current_da,
                                  int16_t target_da,
                                  uint16_t ramp_da_per_s,
                                  uint16_t delta_ms) {
    int32_t allowed_step_da;
    int32_t delta_da;

    if (delta_ms == 0U || current_da == target_da) {
        return current_da;
    }

    allowed_step_da = ((int32_t)ramp_da_per_s * (int32_t)delta_ms) / 1000L;
    if (allowed_step_da <= 0L) {
        allowed_step_da = 1L;
    }

    delta_da = (int32_t)target_da - (int32_t)current_da;
    if (delta_da > allowed_step_da) {
        delta_da = allowed_step_da;
    } else if (delta_da < -allowed_step_da) {
        delta_da = -allowed_step_da;
    }

    return (int16_t)((int32_t)current_da + delta_da);
}

void cluster_current_ramp_init(cluster_current_ramp_t *ramp,
                               uint16_t ramp_up_da_per_s,
                               uint16_t ramp_down_da_per_s) {
    ramp->current_da = 0;
    ramp->ramp_up_da_per_s = ramp_up_da_per_s;
    ramp->ramp_down_da_per_s = ramp_down_da_per_s;
}

int16_t cluster_current_ramp_step(cluster_current_ramp_t *ramp,
                                  int16_t target_da,
                                  uint16_t delta_ms) {
    uint16_t active_ramp_da_per_s;
    active_ramp_da_per_s =
        target_da >= ramp->current_da ? ramp->ramp_up_da_per_s : ramp->ramp_down_da_per_s;
    ramp->current_da = cluster_limit_step(ramp->current_da,
                                          target_da,
                                          active_ramp_da_per_s,
                                          delta_ms);
    return ramp->current_da;
}
