cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(pixelpusher C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(pixelpusher)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/generated)

pico_generate_pio_header(pixelpusher ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/generated)

target_sources(pixelpusher PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/pixelpusher.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_descriptors.c
        )

# Make sure TinyUSB can find tusb_config.h
target_include_directories(pixelpusher PUBLIC
        ${CMAKE_CURRENT_LIST_DIR})

#pico_enable_stdio_usb(pixelpusher 1)

pico_add_extra_outputs(pixelpusher)

target_link_libraries(pixelpusher PUBLIC pico_stdlib tinyusb_device tinyusb_board hardware_pio hardware_dma hardware_pio)

# Additionally generate python and hex pioasm outputs
add_custom_target(pio_ws2812 DEPENDS ${CMAKE_CURRENT_LIST_DIR}/generated/ws2812.py)
add_custom_command(OUTPUT ${CMAKE_CURRENT_LIST_DIR}/generated/ws2812.py
        DEPENDS ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio
        COMMAND pioasm -o python ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio ${CMAKE_CURRENT_LIST_DIR}/generated/ws2812.py
        VERBATIM)
add_dependencies(pixelpusher pio_ws2812)
