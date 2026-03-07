# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(usbprinter C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Root of the shared atariusbprinter source tree
set(PRINTER_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)

set(CORE_SOURCES
    ${PRINTER_ROOT}/src/transport/UsbDeviceDescriptor.cpp
    ${PRINTER_ROOT}/src/protocol/TsplHandler.cpp
    ${PRINTER_ROOT}/src/protocol/PclHandler.cpp
    ${PRINTER_ROOT}/src/protocol/EscpHandler.cpp
    ${PRINTER_ROOT}/src/manager/PrinterManager.cpp
    ${PRINTER_ROOT}/src/util/Logger.cpp
    ${PRINTER_ROOT}/src/generator/ITextGenerator.cpp
    ${PRINTER_ROOT}/src/generator/TsplTextGenerator.cpp
    ${PRINTER_ROOT}/src/generator/PclTextGenerator.cpp
    ${PRINTER_ROOT}/src/generator/EscpTextGenerator.cpp
    ${PRINTER_ROOT}/src/sio/SioProtocol.cpp
    ${PRINTER_ROOT}/src/sio/AtasciiConverter.cpp
    ${PRINTER_ROOT}/src/sio/LineAssembler.cpp
    ${PRINTER_ROOT}/src/sio/SioPrinterEmulator.cpp
)

add_executable(usbprinter
    ${CORE_SOURCES}
    ${PRINTER_ROOT}/platform/rp2040/Rp2040UsbTransport.cpp
    ${PRINTER_ROOT}/platform/rp2040/SioUart.cpp
    ${PRINTER_ROOT}/platform/rp2040/FlashConfig.cpp
    ${PRINTER_ROOT}/platform/rp2040/main_rp2040.cpp
    ${PRINTER_ROOT}/platform/rp2040/posix_stubs.c
)

pico_set_program_name(usbprinter "usbprinter")
pico_set_program_version(usbprinter "0.1")

target_include_directories(usbprinter PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${PRINTER_ROOT}/include
    ${PRINTER_ROOT}/platform/rp2040
)

target_compile_definitions(usbprinter PRIVATE
    PLATFORM_RP2040
    ATARI_PRINTER_PERSONALITY=TSPL
)

target_compile_options(usbprinter PRIVATE
    -fno-exceptions
    $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>
    -Wall
    -Wextra
)

target_link_libraries(usbprinter PRIVATE
    pico_stdlib
    tinyusb_host
    tinyusb_board
    hardware_flash
    hardware_sync
)

pico_enable_stdio_uart(usbprinter 1)
pico_enable_stdio_usb(usbprinter 0)

pico_add_extra_outputs(usbprinter)

