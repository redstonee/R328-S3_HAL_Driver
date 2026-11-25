cmake_minimum_required(VERSION 3.21)
project(
    r328_hal_framework
    VERSION 0.1.0
    LANGUAGES C ASM
)

if(NOT CMAKE_CROSSCOMPILING)
    message(
        FATAL_ERROR
            "Use cmake --preset sram-debug, or pass -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake"
    )
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(R328_MEMORY
    SRAM
    CACHE STRING "Execution memory: SRAM or DDR (already initialized)"
)
set_property(CACHE R328_MEMORY PROPERTY STRINGS SRAM DDR)
set(R328_UART0_ROUTE
    PH0_PH1
    CACHE STRING "UART0 pin route"
)
set_property(CACHE R328_UART0_ROUTE PROPERTY STRINGS PB8_PB9 PH0_PH1)
set(R328_GIC_SECURITY
    SECURE
    CACHE STRING "Execution security state: SECURE or NONSECURE"
)
set_property(CACHE R328_GIC_SECURITY PROPERTY STRINGS SECURE NONSECURE)
set(R328_DDR_ORIGIN
    0x41000000
    CACHE STRING "DDR application address; reserve bootloader memory"
)
set(R328_DDR_LENGTH
    0x00100000
    CACHE STRING "DDR application region in bytes"
)
set(R328_FEL_IMAGE
    OFF
    CACHE BOOL "Build a raw SRAM image for BROM FEL loading"
)
set(R328_FEL_SRAM_PROFILE
    FULL
    CACHE STRING "FEL SRAM layout: FULL (128 KiB, relocating loader) or DIRECT (32 KiB)"
)
set_property(CACHE R328_FEL_SRAM_PROFILE PROPERTY STRINGS FULL DIRECT)
if(NOT R328_FEL_SRAM_PROFILE MATCHES "^(FULL|DIRECT)$")
    message(FATAL_ERROR "R328_FEL_SRAM_PROFILE must be FULL or DIRECT")
endif()
set(R328_XFEL
    xfel
    CACHE STRING "Path to the xfel host utility"
)
if(R328_MEMORY STREQUAL "SRAM")
    set(R328_RAM_ORIGIN 0x00020000)
    set(R328_RAM_LENGTH 0x00020000)
    if(R328_FEL_IMAGE AND R328_FEL_SRAM_PROFILE STREQUAL "DIRECT")
        # Keep the original conservative profile as a fallback.
        set(R328_RAM_ORIGIN 0x00028000)
        set(R328_RAM_LENGTH 0x00008000)
    endif()
elseif(R328_MEMORY STREQUAL "DDR")
    set(R328_RAM_ORIGIN ${R328_DDR_ORIGIN})
    set(R328_RAM_LENGTH ${R328_DDR_LENGTH})
else()
    message(FATAL_ERROR "R328_MEMORY must be SRAM or DDR")
endif()
if(NOT R328_UART0_ROUTE MATCHES "^(PB8_PB9|PH0_PH1)$")
    message(FATAL_ERROR "R328_UART0_ROUTE must be PB8_PB9 or PH0_PH1")
endif()
if(NOT R328_GIC_SECURITY MATCHES "^(SECURE|NONSECURE)$")
    message(FATAL_ERROR "R328_GIC_SECURITY must be SECURE or NONSECURE")
endif()
configure_file(linker/r328_ram.ld.in ${CMAKE_CURRENT_BINARY_DIR}/r328_ram.ld @ONLY)
if(R328_FEL_IMAGE AND (NOT R328_MEMORY STREQUAL "SRAM" OR NOT R328_GIC_SECURITY STREQUAL "SECURE"))
    message(FATAL_ERROR "BROM FEL requires SRAM and SECURE GIC; use --preset fel-debug")
endif()

add_library(r328_options INTERFACE)
target_compile_options(
    r328_options
    INTERFACE -mcpu=cortex-a7
              -marm
              -mfloat-abi=soft
              -mno-unaligned-access
              -ffreestanding
              -fno-builtin
              -ffunction-sections
              -fdata-sections
              -Wall
              -Wextra
              -Werror
              -Wshadow
              -Wundef
              $<$<CONFIG:Debug>:-Og;-g3>
)
target_include_directories(
    r328_options INTERFACE Core/Inc BSP/Inc Drivers/Device/Include Drivers/R328_HAL_Driver/Inc
)
target_compile_definitions(r328_options INTERFACE R328_UART0_ROUTE_${R328_UART0_ROUTE}=1)
if(R328_GIC_SECURITY STREQUAL "NONSECURE")
    target_compile_definitions(r328_options INTERFACE R328_GIC_NONSECURE=1)
endif()
if(R328_FEL_IMAGE)
    target_compile_definitions(r328_options INTERFACE R328_BOOT_FEL=1)
endif()

add_library(
    r328_hal STATIC
    Drivers/R328_HAL_Driver/Src/r328_hal.c
    Drivers/R328_HAL_Driver/Src/r328_hal_gpio.c
    Drivers/R328_HAL_Driver/Src/r328_hal_gpio_exti.c
    Drivers/R328_HAL_Driver/Src/r328_hal_rcc.c
    Drivers/R328_HAL_Driver/Src/r328_hal_uart.c
    Drivers/R328_HAL_Driver/Src/r328_hal_spi.c
    Drivers/R328_HAL_Driver/Src/r328_hal_i2c.c
    Drivers/R328_HAL_Driver/Src/r328_hal_i2s.c
    Drivers/R328_HAL_Driver/Src/r328_hal_bus_rcc.c
    Drivers/R328_HAL_Driver/Src/r328_hal_tim.c
    Drivers/R328_HAL_Driver/Src/r328_hal_gic.c
)
target_link_libraries(r328_hal PUBLIC r328_options)

# Pick up application sources copied into Core/Src, including device drivers.
file(GLOB core_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/*.c")
add_executable(
    firmware Core/Startup/startup_r328.S ${core_sources} BSP/Src/board.c BSP/Src/board_bus.c
)
set_target_properties(
    firmware PROPERTIES SUFFIX ".elf" LINK_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/r328_ram.ld
)
target_link_libraries(firmware PRIVATE r328_hal gcc)
target_link_options(
    firmware
    PRIVATE
    -mcpu=cortex-a7
    -marm
    -mfloat-abi=soft
    -nostdlib
    -T${CMAKE_CURRENT_BINARY_DIR}/r328_ram.ld
    -Wl,--gc-sections,--build-id=none,--fatal-warnings,-z,noexecstack
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/firmware.map
    -Wl,--print-memory-usage
)
add_custom_command(
    TARGET firmware
    POST_BUILD
    BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/firmware.bin ${CMAKE_CURRENT_BINARY_DIR}/firmware.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:firmware>
            ${CMAKE_CURRENT_BINARY_DIR}/firmware.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:firmware>
            ${CMAKE_CURRENT_BINARY_DIR}/firmware.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:firmware>
    VERBATIM
)

include(CTest)
if(BUILD_TESTING OR R328_FEL_IMAGE)
    find_package(
        Python3
        COMPONENTS Interpreter
        REQUIRED
    )
endif()
if(R328_FEL_IMAGE)
    set(fel_binary "${CMAKE_CURRENT_BINARY_DIR}/firmware-fel.bin")
    set(fel_manifest "${CMAKE_CURRENT_BINARY_DIR}/firmware-fel.json")
    set(fel_outputs "${fel_binary}" "${fel_manifest}")
    set(fel_loader_args)
    set(fel_loader_deps)
    if(R328_FEL_SRAM_PROFILE STREQUAL "FULL")
        add_library(fel_relocate OBJECT Core/Startup/fel_relocate.S)
        target_link_libraries(fel_relocate PRIVATE r328_options)
        set(fel_loader_template "${CMAKE_CURRENT_BINARY_DIR}/fel-relocate.bin")
        set(fel_loader_binary "${CMAKE_CURRENT_BINARY_DIR}/firmware-fel-loader.bin")
        add_custom_command(
            OUTPUT "${fel_loader_template}"
            COMMAND ${CMAKE_OBJCOPY} -O binary -j .text.fel_relocate $<TARGET_OBJECTS:fel_relocate>
                    "${fel_loader_template}"
            DEPENDS fel_relocate $<TARGET_OBJECTS:fel_relocate>
            COMMAND_EXPAND_LISTS VERBATIM
        )
        list(APPEND fel_outputs "${fel_loader_binary}")
        list(APPEND fel_loader_args --loader "${fel_loader_template}" --loader-output
             "${fel_loader_binary}"
        )
        list(APPEND fel_loader_deps "${fel_loader_template}")
    endif()
    add_custom_command(
        OUTPUT ${fel_outputs}
        COMMAND
            ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/fel_load.py" package --binary
            "${CMAKE_CURRENT_BINARY_DIR}/firmware.bin" --elf $<TARGET_FILE:firmware> --output
            "${fel_binary}" --manifest "${fel_manifest}" --uart0-route "${R328_UART0_ROUTE}"
            --profile "${R328_FEL_SRAM_PROFILE}" ${fel_loader_args}
        DEPENDS ${fel_loader_deps} firmware "${CMAKE_CURRENT_SOURCE_DIR}/tools/fel_load.py"
        VERBATIM
    )
    add_custom_target(fel-image ALL DEPENDS ${fel_outputs})
    add_custom_target(
        fel-run
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/fel_load.py" run --manifest
                "${fel_manifest}" --xfel "${R328_XFEL}"
        DEPENDS fel-image
        USES_TERMINAL VERBATIM
    )
    if(BUILD_TESTING)
        add_test(NAME fel_image_layout
                 COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/fel_load.py" run
                         --manifest "${fel_manifest}" --dry-run
        )
        if(R328_FEL_SRAM_PROFILE STREQUAL "FULL")
            find_program(R328_QEMU_ARM qemu-system-arm)
            if(R328_QEMU_ARM)
                add_test(
                    NAME fel_arm_relocation
                    COMMAND
                        ${Python3_EXECUTABLE}
                        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_fel_relocate.py" --loader
                        "${fel_loader_template}" --cc "${CMAKE_C_COMPILER}" --objcopy
                        "${CMAKE_OBJCOPY}" --qemu "${R328_QEMU_ARM}"
                )
                set_tests_properties(fel_arm_relocation PROPERTIES TIMEOUT 75)
            endif()
        endif()
    endif()
endif()
if(BUILD_TESTING)
    add_test(
        NAME firmware_layout
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_firmware.py --elf
            $<TARGET_FILE:firmware> --binary ${CMAKE_CURRENT_BINARY_DIR}/firmware.bin --nm
            ${CMAKE_NM} --objdump ${CMAKE_OBJDUMP} --origin ${R328_RAM_ORIGIN} --length
            ${R328_RAM_LENGTH}
    )
endif()
message(
    STATUS
        "R328: ${R328_MEMORY}, origin=${R328_RAM_ORIGIN}, size=${R328_RAM_LENGTH}, UART0=${R328_UART0_ROUTE}, GIC=${R328_GIC_SECURITY}"
)
