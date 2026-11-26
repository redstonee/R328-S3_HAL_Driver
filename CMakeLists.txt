cmake_minimum_required(VERSION 3.21)
project(
    r328_hal_framework
    VERSION 0.1.0
    LANGUAGES C ASM
)

if(NOT CMAKE_CROSSCOMPILING)
    message(
        FATAL_ERROR
            "Use cmake --preset fel-debug, or pass -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake"
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
    0x40000000
    CACHE STRING "DDR application address; reserve loader/security firmware memory"
)
set(R328_DDR_LENGTH
    0x08000000
    CACHE STRING "DDR application region in bytes"
)
set(R328_NOR_IMAGE
    OFF
    CACHE BOOL "Pack boot0 and the application for W25Q64 SPI NOR cold boot"
)
set(R328_NAND_IMAGE
    OFF
    CACHE BOOL "Pack boot0 and the application for direct SPI NAND cold boot"
)
set(R328_NAND_PROFILE
    "${CMAKE_CURRENT_SOURCE_DIR}/prebuilt/r328/spinand.json"
    CACHE FILEPATH "Reviewed SPI NAND profile (default MX35LF1GE4AB)"
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
    math(EXPR ddr_end "${R328_DDR_ORIGIN} + ${R328_DDR_LENGTH}")
    if(R328_DDR_ORIGIN LESS 0x40000000
       OR R328_DDR_LENGTH LESS_EQUAL 0
       OR ddr_end GREATER 0x48000000
    )
        message(FATAL_ERROR "DDR application must fit the configured 128 MiB at 0x40000000")
    endif()
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
if(R328_NAND_IMAGE AND R328_NOR_IMAGE)
    message(FATAL_ERROR "Select only one Flash medium: SPI NAND or SPI NOR")
endif()
if(R328_NAND_IMAGE OR R328_NOR_IMAGE)
    if(R328_FEL_IMAGE
       OR NOT R328_MEMORY STREQUAL "DDR"
       OR NOT R328_GIC_SECURITY STREQUAL "SECURE"
    )
        message(FATAL_ERROR "Flash boot0 requires DDR and SECURE GIC; use nand-debug or nor-debug")
    endif()
    math(EXPR flash_origin "${R328_DDR_ORIGIN}")
    math(EXPR flash_length "${R328_DDR_LENGTH}")
    if(NOT flash_origin EQUAL 0x40000000 OR NOT flash_length EQUAL 0x08000000)
        message(FATAL_ERROR "Flash application must use 0x40000000 and the 128 MiB DDR window")
    endif()
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
    Drivers/R328_HAL_Driver/Src/r328_hal_ledc.c
    Drivers/R328_HAL_Driver/Src/r328_hal_bus_rcc.c
    Drivers/R328_HAL_Driver/Src/r328_hal_tim.c
    Drivers/R328_HAL_Driver/Src/r328_hal_gic.c
)
target_link_libraries(r328_hal PUBLIC r328_options)

# Pick up application sources copied into Core/Src, including device drivers.
file(GLOB core_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/*.c")
add_executable(
    firmware Core/Startup/startup_r328.S ${core_sources} BSP/Src/board.c BSP/Src/board_bus.c
             BSP/Src/board_ledc.c
)
set_target_properties(
    firmware PROPERTIES SUFFIX ".elf" LINK_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/r328_ram.ld
)
if(R328_NAND_IMAGE OR R328_NOR_IMAGE)
    target_sources(firmware PRIVATE Core/Startup/flash_header.S)
endif()
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
if(BUILD_TESTING
   OR R328_FEL_IMAGE
   OR R328_NAND_IMAGE
   OR R328_NOR_IMAGE
)
    find_package(
        Python3
        COMPONENTS Interpreter
        REQUIRED
    )
endif()
if(R328_NAND_IMAGE)
    set(nand_tool "${CMAKE_CURRENT_SOURCE_DIR}/tools/nand_image.py")
    set(nand_prebuilt "${CMAKE_CURRENT_SOURCE_DIR}/prebuilt/r328/boot0-spinand.bin")
    set(nand_profile "${R328_NAND_PROFILE}")
    set(nand_image "${CMAKE_CURRENT_BINARY_DIR}/firmware-nand.bin")
    set(nand_package "${CMAKE_CURRENT_BINARY_DIR}/firmware.toc1")
    set(nand_boot0 "${CMAKE_CURRENT_BINARY_DIR}/boot0.bin")
    set(nand_manifest "${CMAKE_CURRENT_BINARY_DIR}/firmware-nand.json")
    set(nand_arguments
        --boot0
        "${nand_prebuilt}"
        --profile
        "${nand_profile}"
        --firmware
        "${CMAKE_CURRENT_BINARY_DIR}/firmware.bin"
        --elf
        $<TARGET_FILE:firmware>
        --uart0-route
        "${R328_UART0_ROUTE}"
        --output
        "${nand_image}"
        --package
        "${nand_package}"
        --configured-boot0
        "${nand_boot0}"
        --manifest
        "${nand_manifest}"
    )
    add_custom_command(
        OUTPUT "${nand_image}" "${nand_package}" "${nand_boot0}" "${nand_manifest}"
        COMMAND ${Python3_EXECUTABLE} "${nand_tool}" pack ${nand_arguments}
        DEPENDS firmware "${nand_tool}" "${nand_prebuilt}" "${nand_profile}"
        VERBATIM
    )
    add_custom_target(
        flash-image ALL DEPENDS "${nand_image}" "${nand_package}" "${nand_boot0}"
                                "${nand_manifest}"
    )
    file(READ "${nand_profile}" nand_profile_data)
    string(JSON nand_model GET "${nand_profile_data}" model)
    if(nand_model STREQUAL "MX35LF1GE4AB")
        add_executable(fel_nand_helper boot/nand/fel_nand.c boot/nand/fel_nand_entry.S)
        set_target_properties(
            fel_nand_helper
            PROPERTIES SUFFIX ".elf" LINK_DEPENDS
                                     "${CMAKE_CURRENT_SOURCE_DIR}/boot/nand/fel_nand.ld"
        )
        target_compile_options(
            fel_nand_helper
            PRIVATE -mcpu=cortex-a7
                    -mthumb
                    -mfloat-abi=soft
                    -mno-unaligned-access
                    -Os
                    -ffreestanding
                    -fno-builtin
                    -fno-stack-protector
                    -fno-unwind-tables
                    -fno-asynchronous-unwind-tables
                    -ffunction-sections
                    -fdata-sections
                    -Wall
                    -Wextra
                    -Werror
                    -Wshadow
                    -Wundef
        )
        target_link_options(
            fel_nand_helper
            PRIVATE
            -mcpu=cortex-a7
            -mthumb
            -mfloat-abi=soft
            -nostdlib
            "-T${CMAKE_CURRENT_SOURCE_DIR}/boot/nand/fel_nand.ld"
            -Wl,--gc-sections,--build-id=none,--fatal-warnings,-z,noexecstack
            "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/fel-nand-helper.map"
        )
        set(nand_fel_tool "${CMAKE_CURRENT_SOURCE_DIR}/tools/fel_nand.py")
        set(nand_helper_binary "${CMAKE_CURRENT_BINARY_DIR}/fel-nand-helper.bin")
        set(nand_helper_manifest "${CMAKE_CURRENT_BINARY_DIR}/fel-nand-helper.json")
        add_custom_command(
            OUTPUT "${nand_helper_binary}" "${nand_helper_manifest}"
            COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:fel_nand_helper>
                    "${nand_helper_binary}"
            COMMAND
                ${Python3_EXECUTABLE} "${nand_fel_tool}" package --binary "${nand_helper_binary}"
                --elf $<TARGET_FILE:fel_nand_helper> --manifest "${nand_helper_manifest}"
            DEPENDS fel_nand_helper "${nand_fel_tool}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/fel_nor.py" "${nand_tool}"
            VERBATIM
        )
        add_custom_target(
            fel-nand-helper ALL DEPENDS "${nand_helper_binary}" "${nand_helper_manifest}"
        )
        add_custom_target(
            fel-probe
            COMMAND ${Python3_EXECUTABLE} "${nand_fel_tool}" probe --helper-manifest
                    "${nand_helper_manifest}" --xfel "${R328_XFEL}"
            DEPENDS fel-nand-helper
            USES_TERMINAL VERBATIM
        )
        foreach(nand_operation IN ITEMS flash verify)
            add_custom_target(
                fel-${nand_operation}
                COMMAND
                    ${Python3_EXECUTABLE} "${nand_fel_tool}" ${nand_operation} --helper-manifest
                    "${nand_helper_manifest}" --xfel "${R328_XFEL}" --image "${nand_image}"
                    --manifest "${nand_manifest}"
                DEPENDS flash-image fel-nand-helper
                USES_TERMINAL VERBATIM
            )
        endforeach()
    else()
        message(STATUS "FEL NAND flashing supports MX35LF1GE4AB; ${nand_model} image packing only")
    endif()
    if(BUILD_TESTING)
        add_test(NAME nand_image_integrity COMMAND ${Python3_EXECUTABLE} "${nand_tool}" verify
                                                   ${nand_arguments}
        )
        if(TARGET fel_nand_helper)
            add_test(
                NAME fel_nand_layout
                COMMAND
                    ${Python3_EXECUTABLE} "${nand_fel_tool}" flash --helper-manifest
                    "${nand_helper_manifest}" --image "${nand_image}" --manifest "${nand_manifest}"
                    --dry-run
            )
        endif()
        find_program(R328_QEMU qemu-system-arm)
        if(R328_QEMU)
            add_test(
                NAME nand_boot_handoff
                COMMAND
                    ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_nand_boot.py"
                    --cc "${CMAKE_C_COMPILER}" --objcopy "${CMAKE_OBJCOPY}" --qemu "${R328_QEMU}"
            )
            set_tests_properties(nand_boot_handoff PROPERTIES TIMEOUT 60)
            if(TARGET fel_nand_helper)
                add_test(
                    NAME fel_nand_return
                    COMMAND
                        ${Python3_EXECUTABLE}
                        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_fel_nor_entry.py" --cc
                        "${CMAKE_C_COMPILER}" --objcopy "${CMAKE_OBJCOPY}" --qemu "${R328_QEMU}"
                        --medium nand
                )
                set_tests_properties(fel_nand_return PROPERTIES TIMEOUT 30)
            endif()
        endif()
    endif()
endif()
if(R328_NOR_IMAGE)
    add_executable(nor_reader boot/nor/boot0_nor.c)
    set_target_properties(
        nor_reader PROPERTIES SUFFIX ".elf" LINK_DEPENDS
                                            "${CMAKE_CURRENT_SOURCE_DIR}/boot/nor/boot0_nor.ld"
    )
    target_compile_options(
        nor_reader
        PRIVATE -mcpu=cortex-a7
                -mthumb
                -mfloat-abi=soft
                -mno-unaligned-access
                -Os
                -ffreestanding
                -fno-builtin
                -fno-stack-protector
                -fno-unwind-tables
                -fno-asynchronous-unwind-tables
                -ffunction-sections
                -fdata-sections
                -Wall
                -Wextra
                -Werror
                -Wshadow
                -Wundef
    )
    target_link_options(
        nor_reader
        PRIVATE
        -mcpu=cortex-a7
        -mthumb
        -mfloat-abi=soft
        -nostdlib
        "-T${CMAKE_CURRENT_SOURCE_DIR}/boot/nor/boot0_nor.ld"
        -Wl,--gc-sections,--build-id=none,--fatal-warnings,-z,noexecstack
        "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/nor-reader.map"
    )
    set(nor_reader_binary "${CMAKE_CURRENT_BINARY_DIR}/nor-reader.bin")
    add_custom_command(
        OUTPUT "${nor_reader_binary}"
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:nor_reader> "${nor_reader_binary}"
        DEPENDS nor_reader
        VERBATIM
    )
    set(nor_tool "${CMAKE_CURRENT_SOURCE_DIR}/tools/nor_image.py")
    set(nor_prebuilt "${CMAKE_CURRENT_SOURCE_DIR}/prebuilt/r328/boot0-spinand.bin")
    set(nor_profile "${CMAKE_CURRENT_SOURCE_DIR}/prebuilt/r328/spinor.json")
    set(nor_image "${CMAKE_CURRENT_BINARY_DIR}/firmware-nor.bin")
    set(nor_package "${CMAKE_CURRENT_BINARY_DIR}/firmware.toc1")
    set(nor_boot0 "${CMAKE_CURRENT_BINARY_DIR}/boot0.bin")
    set(nor_manifest "${CMAKE_CURRENT_BINARY_DIR}/firmware-nor.json")
    set(nor_arguments
        --boot0
        "${nor_prebuilt}"
        --profile
        "${nor_profile}"
        --reader
        "${nor_reader_binary}"
        --reader-elf
        $<TARGET_FILE:nor_reader>
        --firmware
        "${CMAKE_CURRENT_BINARY_DIR}/firmware.bin"
        --elf
        $<TARGET_FILE:firmware>
        --uart0-route
        "${R328_UART0_ROUTE}"
        --output
        "${nor_image}"
        --package
        "${nor_package}"
        --configured-boot0
        "${nor_boot0}"
        --manifest
        "${nor_manifest}"
    )
    add_custom_command(
        OUTPUT "${nor_image}" "${nor_package}" "${nor_boot0}" "${nor_manifest}"
        COMMAND ${Python3_EXECUTABLE} "${nor_tool}" pack ${nor_arguments}
        DEPENDS firmware "${nor_reader_binary}" "${nor_tool}" "${nor_prebuilt}" "${nor_profile}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/nand_image.py"
        VERBATIM
    )
    add_custom_target(
        flash-image ALL DEPENDS "${nor_image}" "${nor_package}" "${nor_boot0}" "${nor_manifest}"
    )
    # This helper returns to BROM after every operation, so stock xfel can
    # keep transferring chunks without replacing its R328 backend.
    add_executable(fel_nor_helper boot/nor/fel_nor.c boot/nor/fel_nor_entry.S)
    set_target_properties(
        fel_nor_helper PROPERTIES SUFFIX ".elf" LINK_DEPENDS
                                                "${CMAKE_CURRENT_SOURCE_DIR}/boot/nor/fel_nor.ld"
    )
    get_target_property(nor_helper_options nor_reader COMPILE_OPTIONS)
    target_compile_options(fel_nor_helper PRIVATE ${nor_helper_options})
    target_link_options(
        fel_nor_helper
        PRIVATE
        -mcpu=cortex-a7
        -mthumb
        -mfloat-abi=soft
        -nostdlib
        "-T${CMAKE_CURRENT_SOURCE_DIR}/boot/nor/fel_nor.ld"
        -Wl,--gc-sections,--build-id=none,--fatal-warnings,-z,noexecstack
        "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/fel-nor-helper.map"
    )
    set(nor_fel_tool "${CMAKE_CURRENT_SOURCE_DIR}/tools/fel_nor.py")
    set(nor_helper_binary "${CMAKE_CURRENT_BINARY_DIR}/fel-nor-helper.bin")
    set(nor_helper_manifest "${CMAKE_CURRENT_BINARY_DIR}/fel-nor-helper.json")
    add_custom_command(
        OUTPUT "${nor_helper_binary}" "${nor_helper_manifest}"
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:fel_nor_helper> "${nor_helper_binary}"
        COMMAND ${Python3_EXECUTABLE} "${nor_fel_tool}" package --binary "${nor_helper_binary}"
                --elf $<TARGET_FILE:fel_nor_helper> --manifest "${nor_helper_manifest}"
        DEPENDS fel_nor_helper "${nor_fel_tool}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/nand_image.py"
        VERBATIM
    )
    add_custom_target(fel-nor-helper ALL DEPENDS "${nor_helper_binary}" "${nor_helper_manifest}")
    add_custom_target(
        fel-probe
        COMMAND ${Python3_EXECUTABLE} "${nor_fel_tool}" probe --helper-manifest
                "${nor_helper_manifest}" --xfel "${R328_XFEL}"
        DEPENDS fel-nor-helper
        USES_TERMINAL VERBATIM
    )
    foreach(nor_operation IN ITEMS flash verify)
        add_custom_target(
            fel-${nor_operation}
            COMMAND
                ${Python3_EXECUTABLE} "${nor_fel_tool}" ${nor_operation} --helper-manifest
                "${nor_helper_manifest}" --xfel "${R328_XFEL}" --image "${nor_image}" --manifest
                "${nor_manifest}"
            DEPENDS flash-image fel-nor-helper
            USES_TERMINAL VERBATIM
        )
    endforeach()
    if(BUILD_TESTING)
        add_test(NAME nor_image_integrity COMMAND ${Python3_EXECUTABLE} "${nor_tool}" verify
                                                  ${nor_arguments}
        )
        add_test(
            NAME fel_nor_layout
            COMMAND
                ${Python3_EXECUTABLE} "${nor_fel_tool}" flash --helper-manifest
                "${nor_helper_manifest}" --image "${nor_image}" --manifest "${nor_manifest}"
                --dry-run
        )
        find_program(R328_QEMU qemu-system-arm)
        if(R328_QEMU)
            add_test(
                NAME nor_boot_handoff
                COMMAND
                    ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_nand_boot.py"
                    --cc "${CMAKE_C_COMPILER}" --objcopy "${CMAKE_OBJCOPY}" --qemu "${R328_QEMU}"
                    --nor-boot0 "${nor_boot0}"
            )
            set_tests_properties(nor_boot_handoff PROPERTIES TIMEOUT 60)
            add_test(
                NAME fel_nor_return
                COMMAND
                    ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_fel_nor_entry.py"
                    --cc "${CMAKE_C_COMPILER}" --objcopy "${CMAKE_OBJCOPY}" --qemu "${R328_QEMU}"
            )
            set_tests_properties(fel_nor_return PROPERTIES TIMEOUT 30)
        endif()
    endif()
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
