# Must be included BEFORE project() — sets up the ARM cross-compilation toolchain.
# Splits path detection from platform_rp2040.cmake so it can run pre-project().

# --- SDK path ---
if(NOT DEFINED PICO_SDK_PATH AND DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif()
if(NOT DEFINED PICO_SDK_PATH)
    set(PICO_SDK_PATH "$ENV{HOME}/.pico-sdk/sdk/2.2.0")
    message(STATUS "PICO_SDK_PATH not set — using VS Code extension default: ${PICO_SDK_PATH}")
endif()
if(NOT EXISTS "${PICO_SDK_PATH}/pico_sdk_init.cmake")
    message(FATAL_ERROR "PICO_SDK_PATH does not contain pico_sdk_init.cmake: ${PICO_SDK_PATH}")
endif()

# --- Toolchain path (arm-none-eabi-gcc) ---
if(NOT DEFINED PICO_TOOLCHAIN_PATH AND DEFINED ENV{PICO_TOOLCHAIN_PATH})
    set(PICO_TOOLCHAIN_PATH $ENV{PICO_TOOLCHAIN_PATH})
endif()
if(NOT DEFINED PICO_TOOLCHAIN_PATH)
    set(PICO_TOOLCHAIN_PATH "$ENV{HOME}/.pico-sdk/toolchain/14_2_Rel1")
    message(STATUS "PICO_TOOLCHAIN_PATH not set — using VS Code extension default: ${PICO_TOOLCHAIN_PATH}")
endif()

# This sets CMAKE_TOOLCHAIN_FILE so the next project() call uses arm-none-eabi-gcc
include(${PICO_SDK_PATH}/pico_sdk_init.cmake)
