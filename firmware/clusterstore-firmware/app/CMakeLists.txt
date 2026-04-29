if(CS_G474_USE_HAL)
    add_library(cs_g474_app_support STATIC
        cs_g474_board_defaults.c
    )
    target_include_directories(cs_g474_app_support
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../bsp/stm32g474/cube_generated/Inc"
    )
    target_link_libraries(cs_g474_app_support
        PUBLIC
            cs_bsp_g474
    )
endif()

add_library(cs_can_bench_runtime STATIC
    cs_can_bench_node.c
)
target_include_directories(cs_can_bench_runtime
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(cs_can_bench_runtime
    PUBLIC
        cs_bsp_g474
)

if(TARGET cs_cluster_bridge_g474)
    add_library(cs_native_node_runtime STATIC
        cs_native_node_runtime.c
    )
    target_include_directories(cs_native_node_runtime
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../bsp/stm32g474"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../node-firmware/include"
    )
    target_link_libraries(cs_native_node_runtime
        PUBLIC
            cs_cluster_bridge_g474
            cs_cluster_node_firmware_core
    )
endif()

if(CS_G474_USE_HAL)
    add_executable(cs_can_bench_g474
        main.c
        ../bsp/stm32g474/startup/startup_stm32g474xx.s
    )
    target_link_libraries(cs_can_bench_g474
        PRIVATE
            cs_can_bench_runtime
            cs_g474_app_support
            cs_cube_g474_generated
    )
    if(NOT MSVC)
        target_compile_options(cs_can_bench_g474 PRIVATE
            -ffunction-sections
            -fdata-sections
        )
        target_link_options(cs_can_bench_g474 PRIVATE
            -T${CMAKE_CURRENT_SOURCE_DIR}/linker/g474_slot_a.ld
            -Wl,--gc-sections
        )
    endif()
endif()

if(CS_G474_USE_HAL AND TARGET cs_native_node_runtime)
    add_executable(cs_native_node_g474_slot_a
        native_main.c
        ../bsp/stm32g474/startup/startup_stm32g474xx.s
    )
    target_link_libraries(cs_native_node_g474_slot_a
        PRIVATE
            cs_native_node_runtime
            cs_g474_app_support
            cs_cube_g474_generated
    )
    if(NOT MSVC)
        target_compile_options(cs_native_node_g474_slot_a PRIVATE
            -ffunction-sections
            -fdata-sections
        )
        target_link_options(cs_native_node_g474_slot_a PRIVATE
            -T${CMAKE_CURRENT_SOURCE_DIR}/linker/g474_slot_a.ld
            -Wl,--gc-sections
        )
    endif()

    add_executable(cs_native_node_g474_slot_b
        native_main.c
        ../bsp/stm32g474/startup/startup_stm32g474xx.s
    )
    target_link_libraries(cs_native_node_g474_slot_b
        PRIVATE
            cs_native_node_runtime
            cs_g474_app_support
            cs_cube_g474_generated
    )
    if(NOT MSVC)
        target_compile_options(cs_native_node_g474_slot_b PRIVATE
            -ffunction-sections
            -fdata-sections
        )
        target_link_options(cs_native_node_g474_slot_b PRIVATE
            -T${CMAKE_CURRENT_SOURCE_DIR}/linker/g474_slot_b.ld
            -Wl,--gc-sections
        )
    endif()
endif()
