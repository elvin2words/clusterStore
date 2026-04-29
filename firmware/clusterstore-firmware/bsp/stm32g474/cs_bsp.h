#ifndef CS_BSP_H
#define CS_BSP_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    void *port;
    uint16_t pin;
    bool active_high;
} cs_bsp_gpio_t;

typedef struct {
    uint32_t timestamp_ms;
    uint32_t pack_voltage_mv;
    uint32_t bus_voltage_mv;
    int32_t current_ma;
    int32_t temperature_deci_c;
    bool current_valid;
    bool voltages_valid;
    bool temperature_valid;
} cs_bsp_measurements_t;

#endif
