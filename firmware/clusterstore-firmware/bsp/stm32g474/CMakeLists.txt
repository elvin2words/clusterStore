add_library(cs_bsp_g474 STATIC
    cs_adc_g474.c
    cs_bsp_g474.c
    cs_can_g474.c
    cs_flash_g474.c
    cs_ina228.c
    cs_iwdg_g474.c
)

target_include_directories(cs_bsp_g474
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../lib/cluster_platform"
)

target_link_libraries(cs_bsp_g474
    PUBLIC
        cs_cluster_platform
)

if(NOT MSVC)
    target_link_libraries(cs_bsp_g474 PUBLIC m)
endif()

if(CS_G474_USE_HAL)
    target_compile_definitions(cs_bsp_g474 PUBLIC CS_G474_USE_HAL=1)
    target_include_directories(cs_bsp_g474
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/cube_generated/Inc"
            "${CMAKE_CURRENT_SOURCE_DIR}/cube_generated/Drivers/STM32G4xx_HAL_Driver/Inc"
            "${CMAKE_CURRENT_SOURCE_DIR}/cube_generated/Drivers/CMSIS/Device/ST/STM32G4xx/Include"
            "${CMAKE_CURRENT_SOURCE_DIR}/cube_generated/Drivers/CMSIS/Include"
    )

    add_library(cs_cube_g474_generated STATIC
        cube_generated/Src/cs_cube_g474_board.c
        cube_generated/Src/stm32g4xx_hal_msp.c
        cube_generated/Src/stm32g4xx_it.c
        cube_generated/Src/system_stm32g4xx.c
    )
    target_include_directories(cs_cube_g474_generated
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/cube_generated/Inc"
            "${CMAKE_CURRENT_SOURCE_DIR}/cube_generated/Drivers/STM32G4xx_HAL_Driver/Inc"
            "${CMAKE_CURRENT_SOURCE_DIR}/cube_generated/Drivers/CMSIS/Device/ST/STM32G4xx/Include"
            "${CMAKE_CURRENT_SOURCE_DIR}/cube_generated/Drivers/CMSIS/Include"
    )
    target_link_libraries(cs_cube_g474_generated
        PUBLIC
            cs_bsp_g474
    )
endif()
