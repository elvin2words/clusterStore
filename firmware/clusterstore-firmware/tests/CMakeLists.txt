add_library(cs_flash_sim STATIC
    fixtures/flash_sim.c
)
target_include_directories(cs_flash_sim
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
target_link_libraries(cs_flash_sim
    PUBLIC
        cs_cluster_platform
)

add_library(cluster_platform_sim STATIC
    fixtures/cluster_platform_sim.c
)
target_include_directories(cluster_platform_sim
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
target_link_libraries(cluster_platform_sim
    PUBLIC
        cs_cluster_node_firmware_core
)

add_executable(test_boot_control
    test_boot_control.c
)
target_link_libraries(test_boot_control
    PRIVATE
        cs_boot_control
        cs_flash_sim
)
add_test(NAME test_boot_control COMMAND test_boot_control)

add_executable(test_journal
    test_journal.c
)
target_link_libraries(test_journal
    PRIVATE
        cs_journal
        cs_flash_sim
)
add_test(NAME test_journal COMMAND test_journal)

add_executable(test_cluster_platform
    test_cluster_platform.c
)
target_link_libraries(test_cluster_platform
    PRIVATE
        cs_cluster_platform
        cs_flash_sim
)
add_test(NAME test_cluster_platform COMMAND test_cluster_platform)

add_executable(test_g474_bsp
    test_g474_bsp.c
    ../app/cs_can_bench_node.c
)
target_include_directories(test_g474_bsp
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../app"
)
target_link_libraries(test_g474_bsp
    PRIVATE
        cs_bsp_g474
)
add_test(NAME test_g474_bsp COMMAND test_g474_bsp)

add_executable(test_node_boot_control
    test_node_boot_control.c
)
target_link_libraries(test_node_boot_control
    PRIVATE
        cs_cluster_node_firmware_core
        cluster_platform_sim
)
add_test(NAME test_node_boot_control COMMAND test_node_boot_control)

add_executable(test_node_persistent_state
    test_node_persistent_state.c
)
target_link_libraries(test_node_persistent_state
    PRIVATE
        cs_cluster_node_firmware_core
        cluster_platform_sim
)
add_test(NAME test_node_persistent_state COMMAND test_node_persistent_state)
