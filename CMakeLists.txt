# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


set(PICO_BOARD pico_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(ControlaChaoDeFabricaMQTT C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()


include_directories(${CMAKE_SOURCE_DIR}/lib)

# Add executable. Default name is the project name, version 0.1

add_executable(ControlaChaoDeFabricaMQTT ControlaChaoDeFabricaMQTT.c lib/leds.c lib/ssd1306.c)

pico_set_program_name(ControlaChaoDeFabricaMQTT "ControlaChaoDeFabricaMQTT")
pico_set_program_version(ControlaChaoDeFabricaMQTT "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(ControlaChaoDeFabricaMQTT 0)
pico_enable_stdio_usb(ControlaChaoDeFabricaMQTT 1)

# Add the standard library to the build
target_link_libraries(ControlaChaoDeFabricaMQTT
        hardware_pio
        hardware_pwm
        hardware_i2c
        hardware_clocks
        pico_cyw43_arch_lwip_threadsafe_background
        pico_lwip_mqtt
        pico_mbedtls
        pico_lwip_mbedtls
        pico_stdlib)

# Add the standard include files to the build
target_include_directories(ControlaChaoDeFabricaMQTT PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${PICO_SDK_PATH}/lib/lwip/src/include
        ${PICO_SDK_PATH}/lib/lwip/src/include/arch
        ${PICO_SDK_PATH}/lib/lwip/src/include/lwip
)


# Generate PIO header
pico_generate_pio_header(ControlaChaoDeFabricaMQTT ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio)


pico_add_extra_outputs(ControlaChaoDeFabricaMQTT)

