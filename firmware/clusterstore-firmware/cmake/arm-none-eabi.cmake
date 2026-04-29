set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_AR arm-none-eabi-gcc-ar)
set(CMAKE_RANLIB arm-none-eabi-gcc-ranlib)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CS_ARM_COMMON_FLAGS
    "-mcpu=cortex-m4 -mthumb -Os -ffunction-sections -fdata-sections"
)

set(CMAKE_C_FLAGS_INIT "${CS_ARM_COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${CS_ARM_COMMON_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT "-mcpu=cortex-m4 -mthumb")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-mcpu=cortex-m4 -mthumb --specs=nano.specs")
