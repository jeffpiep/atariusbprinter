include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
)
FetchContent_MakeAvailable(Catch2)

# Mock transport library
add_library(mock_usb_transport STATIC
    mock/MockUsbTransport.cpp
)
target_include_directories(mock_usb_transport PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(mock_usb_transport PUBLIC PLATFORM_LINUX)
target_compile_options(mock_usb_transport PRIVATE -fno-exceptions -Wall -Wextra)

# Helper to register a test executable
function(add_driver_test name sources)
    add_executable(${name} ${sources})
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_compile_definitions(${name} PRIVATE PLATFORM_LINUX)
    target_compile_options(${name} PRIVATE -fno-exceptions -Wall -Wextra)
    target_link_libraries(${name} PRIVATE
        printer_driver_core
        mock_usb_transport
        Catch2::Catch2WithMain
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# Phase 1
add_driver_test(test_tspl_handler    test_tspl_handler.cpp)
add_driver_test(test_printer_manager test_printer_manager.cpp)

# Phase 2
add_driver_test(test_ring_buffer     test_ring_buffer.cpp)

# Phase 3
add_driver_test(test_pcl_handler     test_pcl_handler.cpp)
add_driver_test(test_escp_handler    test_escp_handler.cpp)

# Phase 4
add_driver_test(test_tspl_text_generator test_tspl_text_generator.cpp)
add_driver_test(test_pcl_text_generator  test_pcl_text_generator.cpp)
add_driver_test(test_escp_text_generator test_escp_text_generator.cpp)

# Phase 5
add_driver_test(test_atascii_converter   test_atascii_converter.cpp)
add_driver_test(test_line_assembler      test_line_assembler.cpp)
add_driver_test(test_sio_printer_emulator test_sio_printer_emulator.cpp)

# Phase 7 — ESC ~ TSPL config sequences
add_driver_test(test_line_assembler_esc  test_line_assembler_esc.cpp)
