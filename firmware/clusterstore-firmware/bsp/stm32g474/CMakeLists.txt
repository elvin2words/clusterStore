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
    set(CS_G474_HAL_COMPILE_DEFINITIONS
        CS_G474_USE_HAL=1
        STM32G474xx
        USE_HAL_DRIVER
    )
    set(CS_G474_HAL_DRIVER_SOURCES
        cube_generated/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal.c
        cube_generated/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_adc.c
        cube_generated/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_adc_ex.c
        cube_generated/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_cortex.c
        cube_generated/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_dma.c
        cube_generated/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_dma_ex.c
        cube_generated/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_exti.c
        cube_generated/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_fdcan.c
        cube_generated/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash.c
        cube_generated/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash_ex.c
        cube_generated/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash_ramfunc.c
        cube_generated/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_gpio.c
        cube_generated/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_i2c.c
        cube_generated/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_i2c_ex.c
        cube_generated/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_iwdg.c
        cube_generated/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_pwr.c
        cube_generated/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_pwr_ex.c
        cube_generated/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_rcc.c
        cube_generated/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_rcc_ex.c
        cube_generated/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_uart.c
        cube_generated/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_uart_ex.c
    )

    target_compile_definitions(cs_bsp_g474 PUBLIC ${CS_G474_HAL_COMPILE_DEFINITIONS})
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
        ${CS_G474_HAL_DRIVER_SOURCES}
    )
    target_compile_definitions(cs_cube_g474_generated
        PUBLIC
            ${CS_G474_HAL_COMPILE_DEFINITIONS}
    )
    target_include_directories(cs_cube_g474_generated
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/cube_generated/Inc"
            "${CMAKE_CURRENT_SOURCE_DIR}/cube_generated/Drivers/STM32G4xx_HAL_Driver/Inc"
            "${CMAKE_CURRENT_SOURCE_DIR}/cube_generated/Drivers/CMSIS/Device/ST/STM32G4xx/Include"
            "${CMAKE_CURRENT_SOURCE_DIR}/cube_generated/Drivers/CMSIS/Include"
    )

    if(CMAKE_CROSSCOMPILING)
        add_library(cs_g474_syscalls STATIC
            cs_syscalls.c
        )
    endif()
endif()
