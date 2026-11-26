cmake_minimum_required(VERSION 3.21)
project(r328_hal_host_tests LANGUAGES C)
if(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "Host tests require the native compiler; use a separate build directory")
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
enable_testing()
set(HAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../Drivers/R328_HAL_Driver)
add_executable(
    hal_register_tests
    hal_register_tests.c ${HAL_DIR}/Src/r328_hal_gpio.c ${HAL_DIR}/Src/r328_hal_uart.c
    ${HAL_DIR}/Src/r328_hal_rcc.c ${HAL_DIR}/Src/r328_hal_tim.c ${HAL_DIR}/Src/r328_hal_gic.c
)
target_include_directories(
    hal_register_tests PRIVATE ${HAL_DIR}/Inc ../Drivers/Device/Include ../Core/Inc
)
target_compile_definitions(hal_register_tests PRIVATE R328_HAL_HOST_TEST=1 _DEFAULT_SOURCE=1)
target_compile_options(hal_register_tests PRIVATE -Wall -Wextra -Werror -Wshadow -UNDEBUG)
add_test(NAME hal_register_logic COMMAND hal_register_tests)
add_executable(
    bus_register_tests
    bus_register_tests.c
    ${HAL_DIR}/Src/r328_hal_spi.c
    ${HAL_DIR}/Src/r328_hal_i2c.c
    ${HAL_DIR}/Src/r328_hal_dma.c
    ${HAL_DIR}/Src/r328_hal_gic.c
    ${HAL_DIR}/Src/r328_hal_i2s.c
    ${HAL_DIR}/Src/r328_hal_bus_rcc.c
    ${HAL_DIR}/Src/r328_hal_rcc.c
    ${HAL_DIR}/Src/r328_hal_gpio.c
    bus_test_setup.c
)
target_include_directories(
    bus_register_tests PRIVATE ${HAL_DIR}/Inc ../Drivers/Device/Include ../Core/Inc
)
target_compile_definitions(bus_register_tests PRIVATE R328_HAL_HOST_TEST=1 _DEFAULT_SOURCE=1)
target_compile_options(bus_register_tests PRIVATE -Wall -Wextra -Werror -Wshadow -UNDEBUG)
add_test(NAME bus_register_logic COMMAND bus_register_tests)
add_executable(
    spi_async_tests spi_async_tests.c ${HAL_DIR}/Src/r328_hal_spi.c
                    ${HAL_DIR}/Src/r328_hal_dma.c ${HAL_DIR}/Src/r328_hal_gic.c
                    ${HAL_DIR}/Src/r328_hal_bus_rcc.c
)
target_include_directories(
    spi_async_tests PRIVATE ${HAL_DIR}/Inc ../Drivers/Device/Include ../Core/Inc
)
target_compile_definitions(spi_async_tests PRIVATE R328_HAL_HOST_TEST=1 _DEFAULT_SOURCE=1)
target_compile_options(spi_async_tests PRIVATE -Wall -Wextra -Werror -Wshadow -UNDEBUG)
add_test(NAME spi_async_logic COMMAND spi_async_tests)
add_executable(bmp280_tests bmp280_tests.c ../example/i2c/bmp280/bmp280.c)
target_include_directories(bmp280_tests PRIVATE ../example/i2c/bmp280)
target_compile_options(bmp280_tests PRIVATE -Wall -Wextra -Werror -Wshadow -UNDEBUG)
add_test(NAME bmp280_compensation COMMAND bmp280_tests)
add_executable(
    ledc_register_tests
    ledc_register_tests.c ${HAL_DIR}/Src/r328_hal_ledc.c ${HAL_DIR}/Src/r328_hal_bus_rcc.c
    ${HAL_DIR}/Src/r328_hal_gpio.c ledc_test_setup.c
)
target_include_directories(
    ledc_register_tests PRIVATE ${HAL_DIR}/Inc ../Drivers/Device/Include ../Core/Inc
)
target_compile_definitions(ledc_register_tests PRIVATE R328_HAL_HOST_TEST=1 _DEFAULT_SOURCE=1)
target_compile_options(ledc_register_tests PRIVATE -Wall -Wextra -Werror -Wshadow -UNDEBUG)
add_test(NAME ledc_register_logic COMMAND ledc_register_tests)
add_executable(hal_timebase_tests hal_timebase_tests.c ${HAL_DIR}/Src/r328_hal.c)
target_include_directories(
    hal_timebase_tests PRIVATE ${HAL_DIR}/Inc ../Drivers/Device/Include ../Core/Inc
)
target_compile_definitions(hal_timebase_tests PRIVATE R328_HAL_HOST_TEST=1)
target_compile_options(hal_timebase_tests PRIVATE -Wall -Wextra -Werror -Wshadow -UNDEBUG)
add_test(NAME hal_timebase COMMAND hal_timebase_tests)
add_executable(
    display_example_tests display_example_tests.c ../example/spi/ili9341_xpt2046/ili9341.c
                          ../example/spi/ili9341_xpt2046/xpt2046.c ../example/spi/ili9341_xpt2046/display_bus.c
)
target_include_directories(
    display_example_tests PRIVATE ${HAL_DIR}/Inc ../Drivers/Device/Include ../Core/Inc
                                  ../example/spi/ili9341_xpt2046
)
target_compile_definitions(display_example_tests PRIVATE R328_HAL_HOST_TEST=1)
target_compile_options(display_example_tests PRIVATE -Wall -Wextra -Werror -Wshadow -UNDEBUG)
add_test(NAME display_example_protocol COMMAND display_example_tests)
find_package(
    Python3
    COMPONENTS Interpreter
    REQUIRED
)
add_test(NAME fel_loader COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_fel_load.py)
add_test(NAME fel_ddr_host COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_fel_dram.py)

add_executable(
    gpio_exti_tests gpio_exti_tests.c ${HAL_DIR}/Src/r328_hal_gpio.c
                    ${HAL_DIR}/Src/r328_hal_gpio_exti.c ${HAL_DIR}/Src/r328_hal_gic.c
)
target_include_directories(
    gpio_exti_tests PRIVATE ${HAL_DIR}/Inc ../Drivers/Device/Include ../Core/Inc
)
target_compile_definitions(gpio_exti_tests PRIVATE R328_HAL_HOST_TEST=1 _DEFAULT_SOURCE=1)
target_compile_options(gpio_exti_tests PRIVATE -Wall -Wextra -Werror -Wshadow -UNDEBUG)
add_test(NAME gpio_exti COMMAND gpio_exti_tests)

add_test(NAME nand_image_format COMMAND ${Python3_EXECUTABLE}
                                        ${CMAKE_CURRENT_SOURCE_DIR}/test_nand_image.py
)

add_executable(nor_boot_tests nor_boot_tests.c)
target_compile_options(nor_boot_tests PRIVATE -Wall -Wextra -Werror -Wshadow -UNDEBUG)
add_test(NAME nor_boot_reader COMMAND nor_boot_tests)
add_test(NAME nor_image_format COMMAND ${Python3_EXECUTABLE}
                                       ${CMAKE_CURRENT_SOURCE_DIR}/test_nor_image.py
)

add_executable(fel_nor_tests fel_nor_tests.c)
target_compile_options(fel_nor_tests PRIVATE -Wall -Wextra -Werror -Wshadow -UNDEBUG)
add_test(NAME fel_nor_protocol COMMAND fel_nor_tests)
add_test(NAME fel_nor_host COMMAND ${Python3_EXECUTABLE}
                                   ${CMAKE_CURRENT_SOURCE_DIR}/test_fel_nor.py
)

add_executable(fel_nand_tests fel_nand_tests.c)
target_compile_options(fel_nand_tests PRIVATE -Wall -Wextra -Werror -Wshadow -UNDEBUG)
add_test(NAME fel_nand_protocol COMMAND fel_nand_tests)
add_test(NAME fel_nand_host COMMAND ${Python3_EXECUTABLE}
                                    ${CMAKE_CURRENT_SOURCE_DIR}/test_fel_nand.py
)
