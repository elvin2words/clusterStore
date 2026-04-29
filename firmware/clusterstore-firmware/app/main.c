#include "cs_can_bench_node.h"
#include "cs_cube_g474_board.h"
#include "cs_g474_board_defaults.h"

static cs_bsp_g474_t g_bsp;
static cs_can_bench_node_t g_bench_node;

void cs_cube_g474_on_fdcan_rx_fifo0_irq(void) {
    cs_bsp_g474_on_fdcan_rx_fifo0_irq(&g_bsp);
}

int main(void) {
    const cs_cube_g474_board_handles_t *handles;
    cs_bsp_g474_config_t bsp_config;

    cs_cube_g474_board_init();
    handles = cs_cube_g474_board_handles();
    cs_g474_fill_default_bsp_config(&bsp_config, handles);
    if (cs_bsp_g474_init(&g_bsp, &bsp_config) != CS_STATUS_OK ||
        cs_bsp_g474_start_can(&g_bsp) != CS_STATUS_OK) {
        Error_Handler();
    }

    cs_can_bench_node_init(&g_bench_node, 1U);
    while (1) {
        cs_bsp_g474_on_fdcan_rx_fifo0_irq(&g_bsp);
        if (cs_can_bench_node_step(&g_bench_node, &g_bsp) != CS_STATUS_OK) {
            Error_Handler();
        }
    }
}
