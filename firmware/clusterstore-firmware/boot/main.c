#include "cluster_bootloader_runtime.h"
#include "cluster_stm32_boot.h"
#include "cs_cluster_bridge_g474.h"
#include "cs_cube_g474_board.h"
#include "cs_g474_board_defaults.h"

static cs_bsp_g474_t g_bsp;
static cs_cluster_bridge_g474_t g_bridge;
static cluster_bootloader_runtime_t g_bootloader;

static void cs_fill_boot_flash_layout(cluster_flash_layout_config_t *layout) {
    if (layout == NULL) {
        return;
    }

    layout->flash_base_address = CS_G474_BOOTLOADER_ADDRESS;
    layout->flash_size_bytes = CS_G474_FLASH_SIZE_BYTES;
    layout->bootloader_size_bytes = 32UL * 1024UL;
    layout->metadata_size_bytes = 8UL * 1024UL;
    layout->journal_size_bytes = 24UL * 1024UL;
    layout->slot_size_bytes = 192UL * 1024UL;
    layout->bootloader_address = CS_G474_BOOTLOADER_ADDRESS;
    layout->slot_a_address = CS_G474_SLOT_A_ADDRESS;
    layout->slot_b_address = CS_G474_SLOT_B_ADDRESS;
    layout->metadata_address = CS_G474_BCB_A_ADDRESS;
    layout->journal_address = CS_G474_JOURNAL_ADDRESS;
}

int main(void) {
    const cs_cube_g474_board_handles_t *handles;
    cs_bsp_g474_config_t bsp_config;
    cs_cluster_bridge_g474_config_t bridge_config;
    cluster_bootloader_runtime_config_t boot_config;
    uint8_t selected_slot = 0U;

    cs_cube_g474_board_init();
    handles = cs_cube_g474_board_handles();
    cs_g474_fill_default_bsp_config(&bsp_config, handles);
    if (cs_bsp_g474_init(&g_bsp, &bsp_config) != CS_STATUS_OK) {
        Error_Handler();
    }

    bridge_config.default_soc_pct = 50U;
    bridge_config.local_fault_flags = 0U;
    bridge_config.balancing_supported = false;
    bridge_config.maintenance_lockout = false;
    bridge_config.service_lockout = false;
    if (cs_cluster_bridge_g474_init(&g_bridge, &g_bsp, &bridge_config) !=
        CS_STATUS_OK) {
        Error_Handler();
    }

    boot_config.platform = &g_bridge.cluster_platform;
    cs_fill_boot_flash_layout(&boot_config.flash_layout);
    boot_config.default_active_slot_id = CLUSTER_BOOT_SLOT_A;
    boot_config.default_version = "0.1.0";
    boot_config.verify_crc32_before_boot = true;
    boot_config.verify_image = NULL;
    boot_config.verify_context = NULL;
    boot_config.jump_to_image = cluster_stm32_jump_to_image;
    boot_config.jump_context = NULL;
    if (!cluster_bootloader_runtime_init(&g_bootloader, &boot_config)) {
        Error_Handler();
    }

    while (1) {
        if (cluster_bootloader_runtime_boot(&g_bootloader, &selected_slot) ==
            CLUSTER_BOOT_ACTION_STAY_IN_BOOTLOADER) {
            (void)cs_bsp_g474_watchdog_kick(&g_bsp);
            HAL_Delay(250U);
            continue;
        }
    }
}
