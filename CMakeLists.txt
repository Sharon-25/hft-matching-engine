cmake_minimum_required(VERSION 3.20)

project(
    HFTMatchingEngine
    VERSION 0.1.0
    LANGUAGES CXX
)

# Core matching engine library
add_library(
    matching_core
    STATIC
    src/Order.cpp
    src/OrderBook.cpp
)

target_include_directories(
    matching_core
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(
    matching_core
    PUBLIC
    cxx_std_20
)

set_target_properties(
    matching_core
    PROPERTIES
    CXX_EXTENSIONS OFF
)

# Compiler warnings
if(MSVC)
    target_compile_options(
        matching_core
        PRIVATE
        /W4
        /permissive-
    )
else()
    target_compile_options(
        matching_core
        PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

# Main executable
add_executable(
    matching_engine
    src/main.cpp
)

target_link_libraries(
    matching_engine
    PRIVATE
    matching_core
)

# Testing
include(CTest)

if(BUILD_TESTING)
    add_executable(
        order_tests
        tests/OrderTests.cpp
    )

    target_link_libraries(
        order_tests
        PRIVATE
        matching_core
    )

    add_test(
        NAME OrderTests
        COMMAND order_tests
    )

    add_executable(
        order_book_tests
        tests/OrderBookTests.cpp
    )

    target_link_libraries(
        order_book_tests
        PRIVATE
        matching_core
    )

    add_test(
        NAME OrderBookTests
        COMMAND order_book_tests
    )
endif()
