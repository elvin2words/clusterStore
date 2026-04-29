#include "cs_cube_g474_board.h"
#include "cs_g474_board_defaults.h"
#include "cs_native_node_runtime.h"

static cs_bsp_g474_t g_bsp;
static cs_native_node_runtime_t g_runtime;

void cs_cube_g474_on_fdcan_rx_fifo0_irq(void) {
    cs_bsp_g474_on_fdcan_rx_fifo0_irq(&g_bsp);
}

int main(void) {
    const cs_cube_g474_board_handles_t *handles;
    cs_bsp_g474_config_t bsp_config;
    cs_native_node_runtime_config_t runtime_config;

    cs_cube_g474_board_init();
    handles = cs_cube_g474_board_handles();
    cs_g474_fill_default_bsp_config(&bsp_config, handles);
    if (cs_bsp_g474_init(&g_bsp, &bsp_config) != CS_STATUS_OK ||
        cs_bsp_g474_start_can(&g_bsp) != CS_STATUS_OK) {
        Error_Handler();
    }

    cs_native_node_runtime_config_init(&runtime_config);
    if (cs_native_node_runtime_init(&g_runtime, &g_bsp, &runtime_config) !=
        CS_STATUS_OK) {
        Error_Handler();
    }

    while (1) {
        if (cs_native_node_runtime_step(&g_runtime) != CS_STATUS_OK) {
            Error_Handler();
        }
    }
}
