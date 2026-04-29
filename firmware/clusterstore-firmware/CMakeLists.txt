cmake_minimum_required(VERSION 3.20)

project(clusterstore_firmware C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(CS_BUILD_TESTS "Build host-side firmware unit tests" ON)
option(CS_BUILD_STM32G474_BSP "Build the STM32G474 BSP layer" ON)
option(CS_G474_USE_HAL "Compile the STM32G474 BSP against STM32Cube HAL" OFF)
option(CS_BUILD_CAN_BENCH_APP "Build the STM32G474 CAN-only bench image" OFF)
option(CS_BUILD_NATIVE_NODE_APP "Build the STM32G474 native node image" OFF)
option(CS_BUILD_BOOTLOADER "Build the STM32G474 bootloader image" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(version)

cs_read_version(CS_FIRMWARE_VERSION "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
message(STATUS "ClusterStore firmware version: ${CS_FIRMWARE_VERSION}")

add_library(cs_cluster_platform STATIC
    lib/cluster_platform/cs_cluster_platform.c
)
target_include_directories(cs_cluster_platform
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/lib/cluster_platform"
)

add_library(cs_crc32 STATIC
    lib/boot_control/cs_crc32.c
)
target_include_directories(cs_crc32
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/lib/boot_control"
)

add_library(cs_boot_control STATIC
    lib/boot_control/cs_boot_control.c
)
target_include_directories(cs_boot_control
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/lib/boot_control"
)
target_link_libraries(cs_boot_control
    PUBLIC
        cs_cluster_platform
        cs_crc32
)

add_library(cs_journal STATIC
    lib/journal/cs_journal.c
)
target_include_directories(cs_journal
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/lib/journal"
)
target_link_libraries(cs_journal
    PUBLIC
        cs_cluster_platform
        cs_crc32
)

if(CS_BUILD_STM32G474_BSP OR CS_BUILD_TESTS OR CS_BUILD_CAN_BENCH_APP OR
   CS_BUILD_NATIVE_NODE_APP OR CS_BUILD_BOOTLOADER)
    add_subdirectory(bsp/stm32g474)
endif()

if(CS_BUILD_NATIVE_NODE_APP OR CS_BUILD_BOOTLOADER)
    add_library(cs_cluster_node_firmware_core STATIC
        ../node-firmware/src/cluster_boot_control.c
        ../node-firmware/src/cluster_bootloader_runtime.c
        ../node-firmware/src/cluster_can_protocol.c
        ../node-firmware/src/cluster_command_manager.c
        ../node-firmware/src/cluster_contactor_manager.c
        ../node-firmware/src/cluster_crc32.c
        ../node-firmware/src/cluster_current_ramp.c
        ../node-firmware/src/cluster_event_journal.c
        ../node-firmware/src/cluster_flash_layout.c
        ../node-firmware/src/cluster_node_controller.c
        ../node-firmware/src/cluster_node_runtime.c
        ../node-firmware/src/cluster_ota_manager.c
        ../node-firmware/src/cluster_persistent_state.c
        ../node-firmware/src/cluster_platform.c
        ../node-firmware/src/cluster_state_machine.c
    )
    target_include_directories(cs_cluster_node_firmware_core
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/../node-firmware/include"
    )

    add_library(cs_cluster_node_firmware_stm32 STATIC
        ../node-firmware/src/cluster_stm32_boot.c
        ../node-firmware/src/cluster_stm32_hal.c
    )
    target_include_directories(cs_cluster_node_firmware_stm32
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/../node-firmware/include"
    )
    target_link_libraries(cs_cluster_node_firmware_stm32
        PUBLIC
            cs_cluster_node_firmware_core
    )

    add_library(cs_cluster_bridge_g474 STATIC
        bsp/stm32g474/cs_cluster_bridge_g474.c
    )
    target_include_directories(cs_cluster_bridge_g474
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/bsp/stm32g474"
            "${CMAKE_CURRENT_SOURCE_DIR}/../node-firmware/include"
    )
    target_link_libraries(cs_cluster_bridge_g474
        PUBLIC
            cs_bsp_g474
            cs_cluster_node_firmware_stm32
    )

    if(CS_G474_USE_HAL)
        target_include_directories(cs_cluster_node_firmware_stm32
            PUBLIC
                "${CMAKE_CURRENT_SOURCE_DIR}/bsp/stm32g474/cube_generated/Inc"
                "${CMAKE_CURRENT_SOURCE_DIR}/bsp/stm32g474/cube_generated/Drivers/STM32G4xx_HAL_Driver/Inc"
                "${CMAKE_CURRENT_SOURCE_DIR}/bsp/stm32g474/cube_generated/Drivers/CMSIS/Device/ST/STM32G4xx/Include"
                "${CMAKE_CURRENT_SOURCE_DIR}/bsp/stm32g474/cube_generated/Drivers/CMSIS/Include"
        )
        target_compile_definitions(cs_cluster_node_firmware_stm32
            PUBLIC
                CLUSTER_STM32_HAL_HEADER=\"stm32g4xx_hal.h\"
                CLUSTER_STM32_CMSIS_HEADER=\"stm32g474xx.h\"
        )
        target_compile_definitions(cs_cluster_bridge_g474 PUBLIC CS_G474_USE_HAL=1)
        target_include_directories(cs_cluster_bridge_g474
            PUBLIC
                "${CMAKE_CURRENT_SOURCE_DIR}/bsp/stm32g474/cube_generated/Inc"
                "${CMAKE_CURRENT_SOURCE_DIR}/bsp/stm32g474/cube_generated/Drivers/STM32G4xx_HAL_Driver/Inc"
                "${CMAKE_CURRENT_SOURCE_DIR}/bsp/stm32g474/cube_generated/Drivers/CMSIS/Device/ST/STM32G4xx/Include"
                "${CMAKE_CURRENT_SOURCE_DIR}/bsp/stm32g474/cube_generated/Drivers/CMSIS/Include"
        )
    endif()
endif()

if(CS_BUILD_CAN_BENCH_APP OR CS_BUILD_NATIVE_NODE_APP OR CS_BUILD_BOOTLOADER)
    add_subdirectory(app)
endif()

if(CS_BUILD_BOOTLOADER)
    add_subdirectory(boot)
endif()

if(CS_BUILD_TESTS)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
