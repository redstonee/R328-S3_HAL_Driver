add_executable(fel_dram_loader boot/dram/fel_dram.S)
set_target_properties(
    fel_dram_loader
    PROPERTIES SUFFIX ".elf" LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/boot/dram/fel_dram.ld"
)
target_compile_options(fel_dram_loader PRIVATE -mcpu=cortex-a7 -marm -mfloat-abi=soft)
target_link_options(
    fel_dram_loader
    PRIVATE -mcpu=cortex-a7
            -marm
            -mfloat-abi=soft
            -nostdlib
            "-T${CMAKE_CURRENT_SOURCE_DIR}/boot/dram/fel_dram.ld"
            -Wl,--gc-sections,--build-id=none,--fatal-warnings,-z,noexecstack
            "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/fel-dram-loader.map"
)
set(fel_ddr_tool "${CMAKE_CURRENT_SOURCE_DIR}/tools/fel_dram.py")
set(fel_ddr_helper "${CMAKE_CURRENT_BINARY_DIR}/fel-dram-loader.bin")
set(fel_ddr_vendor "${CMAKE_CURRENT_BINARY_DIR}/fel-dram-vendor.bin")
set(fel_ddr_manifest "${CMAKE_CURRENT_BINARY_DIR}/firmware-fel-ddr.json")
set(fel_ddr_prebuilt "${CMAKE_CURRENT_SOURCE_DIR}/prebuilt/r328/boot0-spinand.bin")
add_custom_command(
    OUTPUT "${fel_ddr_helper}"
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:fel_dram_loader> "${fel_ddr_helper}"
    DEPENDS fel_dram_loader
    VERBATIM
)
add_custom_command(
    OUTPUT "${fel_ddr_vendor}" "${fel_ddr_manifest}"
    COMMAND
        ${Python3_EXECUTABLE} "${fel_ddr_tool}" package --binary
        "${CMAKE_CURRENT_BINARY_DIR}/firmware.bin" --elf $<TARGET_FILE:firmware> --helper
        "${fel_ddr_helper}" --helper-elf $<TARGET_FILE:fel_dram_loader> --boot0
        "${fel_ddr_prebuilt}" --vendor-output "${fel_ddr_vendor}" --manifest
        "${fel_ddr_manifest}" --origin "${R328_RAM_ORIGIN}" --length "${R328_RAM_LENGTH}"
    DEPENDS firmware fel_dram_loader "${fel_ddr_helper}" "${fel_ddr_prebuilt}" "${fel_ddr_tool}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/fel_load.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/nand_image.py"
    VERBATIM
)
add_custom_target(
    fel-image ALL DEPENDS "${fel_ddr_manifest}" "${fel_ddr_vendor}" "${fel_ddr_helper}"
)
add_custom_target(
    fel-run
    COMMAND ${Python3_EXECUTABLE} "${fel_ddr_tool}" run --manifest "${fel_ddr_manifest}"
            --xfel "${R328_XFEL}"
    DEPENDS fel-image
    USES_TERMINAL VERBATIM
)
if(BUILD_TESTING)
    add_test(
        NAME fel_ddr_layout
        COMMAND ${Python3_EXECUTABLE} "${fel_ddr_tool}" run --manifest "${fel_ddr_manifest}"
                --dry-run
    )
    find_program(R328_QEMU_ARM qemu-system-arm)
    if(R328_QEMU_ARM)
        add_test(
            NAME fel_ddr_return
            COMMAND
                ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_fel_dram_entry.py"
                --cc "${CMAKE_C_COMPILER}" --objcopy "${CMAKE_OBJCOPY}" --qemu "${R328_QEMU_ARM}"
        )
        set_tests_properties(fel_ddr_return PROPERTIES TIMEOUT 45)
    endif()
endif()
