if(CS_G474_USE_HAL AND TARGET cs_cluster_bridge_g474)
    add_executable(cs_bootloader_g474
        main.c
        ../bsp/stm32g474/startup/startup_stm32g474xx.s
    )
    target_include_directories(cs_bootloader_g474
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../app"
            "${CMAKE_CURRENT_SOURCE_DIR}/../bsp/stm32g474"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../node-firmware/include"
    )
    target_link_libraries(cs_bootloader_g474
        PRIVATE
            cs_cluster_bridge_g474
            cs_cluster_node_firmware_core
            cs_cluster_node_firmware_stm32
            cs_g474_app_support
            cs_cube_g474_generated
    )
    if(NOT MSVC)
        target_compile_options(cs_bootloader_g474 PRIVATE
            -ffunction-sections
            -fdata-sections
        )
        target_link_options(cs_bootloader_g474 PRIVATE
            -T${CMAKE_CURRENT_SOURCE_DIR}/linker/g474_bootloader.ld
            -Wl,--gc-sections
        )
    endif()
endif()
