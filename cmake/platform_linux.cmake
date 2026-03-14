find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

# Common library sources (shared between Linux target and tests)
set(CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/transport/UsbDeviceDescriptor.cpp
    ${CMAKE_SOURCE_DIR}/src/protocol/TsplHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/protocol/PclHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/protocol/EscposHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/manager/PrinterManager.cpp
    ${CMAKE_SOURCE_DIR}/src/util/Logger.cpp
    ${CMAKE_SOURCE_DIR}/src/generator/ITextGenerator.cpp
    ${CMAKE_SOURCE_DIR}/src/generator/TsplTextGenerator.cpp
    ${CMAKE_SOURCE_DIR}/src/generator/PclTextGenerator.cpp
    ${CMAKE_SOURCE_DIR}/src/generator/EscposTextGenerator.cpp
    ${CMAKE_SOURCE_DIR}/src/sio/SioProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/sio/AtasciiConverter.cpp
    ${CMAKE_SOURCE_DIR}/src/sio/LineAssembler.cpp
    ${CMAKE_SOURCE_DIR}/src/sio/SioPrinterEmulator.cpp
    ${CMAKE_SOURCE_DIR}/platform/linux/FlashConfig.cpp
)

add_library(printer_driver_core STATIC ${CORE_SOURCES})
target_include_directories(printer_driver_core PUBLIC
    ${COMMON_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/platform/rp2040   # FlashConfig.h (Linux stub satisfies it)
)
target_compile_options(printer_driver_core PRIVATE -fno-exceptions -Wall -Wextra)
target_compile_definitions(printer_driver_core PUBLIC PLATFORM_LINUX)

# Linux USB transport
add_library(printer_driver_linux_transport STATIC
    ${CMAKE_SOURCE_DIR}/platform/linux/LinuxUsbTransport.cpp
)
target_include_directories(printer_driver_linux_transport PUBLIC
    ${COMMON_INCLUDE_DIR}
    ${LIBUSB_INCLUDE_DIRS}
)
target_compile_options(printer_driver_linux_transport PRIVATE -fno-exceptions -Wall -Wextra)
target_compile_definitions(printer_driver_linux_transport PUBLIC PLATFORM_LINUX)
target_link_libraries(printer_driver_linux_transport PRIVATE ${LIBUSB_LIBRARIES})

# CLI executable
add_executable(tspl_print
    ${CMAKE_SOURCE_DIR}/platform/linux/main_linux.cpp
)
target_include_directories(tspl_print PRIVATE
    ${COMMON_INCLUDE_DIR}
    ${LIBUSB_INCLUDE_DIRS}
)
target_compile_options(tspl_print PRIVATE -fno-exceptions -Wall -Wextra)
target_compile_definitions(tspl_print PRIVATE PLATFORM_LINUX)
target_link_libraries(tspl_print PRIVATE
    printer_driver_core
    printer_driver_linux_transport
    ${LIBUSB_LIBRARIES}
)
