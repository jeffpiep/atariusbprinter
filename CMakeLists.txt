cmake_minimum_required(VERSION 3.20)

# pico-sdk requires its init to run BEFORE project() to set up the ARM toolchain.
# TARGET_RP2040 is set from the command line (-DTARGET_RP2040=ON) so it is
# available here even before the option() declaration below.
if(TARGET_RP2040)
    include(cmake/pico_pre_init.cmake)
endif()

project(usb_printer_driver VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TARGET_RP2040 "Build for RP2040 via pico-sdk" OFF)
option(BUILD_TESTS "Build unit tests" ON)

# Common include path
set(COMMON_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)

if(TARGET_RP2040)
    include(cmake/platform_rp2040.cmake)
else()
    include(cmake/platform_linux.cmake)
endif()

if(BUILD_TESTS AND NOT TARGET_RP2040)
    enable_testing()
    add_subdirectory(tests)
endif()
