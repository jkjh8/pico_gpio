# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# RP2350 Pico 2 설정 (안정적인 기본 설정)
set(PICO_BOARD pico2)
set(PICO_PLATFORM rp2350)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
# RP2350 Pico 2 설정은 상단에서 이미 완료

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(main C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(main 
    main.c 
    network/mac_utils.c
    network/network_config.c
    http_server.c
    http_handlers.c
    static_files.c
)

# define W5500 chip for all source files
target_compile_definitions(main PRIVATE _WIZCHIP_=W5500)

pico_set_program_name(main "main")
pico_set_program_version(main "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(main 1)
pico_enable_stdio_usb(main 0)

# Add the standard library to the build
target_link_libraries(main
        pico_stdlib
        hardware_spi
        hardware_gpio
        hardware_flash
        hardware_sync
        hardware_watchdog
        pico_unique_id
)

# Add the standard include files to the build
target_include_directories(main PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_SOURCE_DIR}/ioLibrary_Driver
        ${CMAKE_SOURCE_DIR}/ioLibrary_Driver/Ethernet
        ${CMAKE_SOURCE_DIR}/ioLibrary_Driver/Internet/DHCP
        ${CMAKE_SOURCE_DIR}/cJSON
)

# Add source files from the latest driver
set(WIZNET_SOURCE
    ${CMAKE_SOURCE_DIR}/ioLibrary_Driver/Ethernet/wizchip_conf.c
    ${CMAKE_SOURCE_DIR}/ioLibrary_Driver/Ethernet/W5500/w5500.c
    ${CMAKE_SOURCE_DIR}/ioLibrary_Driver/Ethernet/socket.c
    ${CMAKE_SOURCE_DIR}/ioLibrary_Driver/Internet/DHCP/dhcp.c
)

# Add cJSON source
set(CJSON_SOURCE
    ${CMAKE_SOURCE_DIR}/cJSON/cJSON.c
)

# Link WIZnet and cJSON source
target_sources(main PRIVATE ${WIZNET_SOURCE} ${CJSON_SOURCE})

pico_add_extra_outputs(main)

