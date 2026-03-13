# RP2040 platform build via pico-sdk.
# Path detection and pico_sdk_init.cmake inclusion happen in cmake/pico_pre_init.cmake
# (included before project() in CMakeLists.txt). This file only calls pico_sdk_init()
# and defines the build target.

pico_sdk_init()

set(CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/transport/UsbDeviceDescriptor.cpp
    ${CMAKE_SOURCE_DIR}/src/protocol/TsplHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/protocol/PclHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/protocol/EscpHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/manager/PrinterManager.cpp
    ${CMAKE_SOURCE_DIR}/src/util/Logger.cpp
    ${CMAKE_SOURCE_DIR}/src/generator/ITextGenerator.cpp
    ${CMAKE_SOURCE_DIR}/src/generator/TsplTextGenerator.cpp
    ${CMAKE_SOURCE_DIR}/src/generator/PclTextGenerator.cpp
    ${CMAKE_SOURCE_DIR}/src/generator/EscpTextGenerator.cpp
    ${CMAKE_SOURCE_DIR}/src/generator/EscposTextGenerator.cpp
    ${CMAKE_SOURCE_DIR}/src/sio/SioProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/sio/AtasciiConverter.cpp
    ${CMAKE_SOURCE_DIR}/src/sio/LineAssembler.cpp
    ${CMAKE_SOURCE_DIR}/src/sio/SioPrinterEmulator.cpp
)

add_executable(usb_printer_rp2040
    ${CORE_SOURCES}
    ${CMAKE_SOURCE_DIR}/platform/rp2040/Rp2040UsbTransport.cpp
    ${CMAKE_SOURCE_DIR}/platform/rp2040/SioUart.cpp
    ${CMAKE_SOURCE_DIR}/platform/rp2040/FlashConfig.cpp
    ${CMAKE_SOURCE_DIR}/platform/rp2040/main_rp2040.cpp
)

target_include_directories(usb_printer_rp2040 PRIVATE
    ${COMMON_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/platform/rp2040
)

target_compile_definitions(usb_printer_rp2040 PRIVATE
    PLATFORM_RP2040
    # Default personality — override with -DATARI_PRINTER_PERSONALITY=1025 etc.
    ATARI_PRINTER_PERSONALITY=TSPL
)

target_compile_options(usb_printer_rp2040 PRIVATE
    -fno-exceptions
    -Wall
    -Wextra
)

target_link_libraries(usb_printer_rp2040 PRIVATE
    pico_stdlib
    tinyusb_host
    tinyusb_board
    hardware_flash
    hardware_sync
)

pico_add_extra_outputs(usb_printer_rp2040)
pico_enable_stdio_usb(usb_printer_rp2040 0)
pico_enable_stdio_uart(usb_printer_rp2040 1)
