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
    src/MatchingEngine.cpp
    src/MatchingWorker.cpp
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
    # Separate instrumented library for testing only.
    # The macro must be consistent between the library
    # and every test executable using its public headers.
    add_library(
        matching_core_testing
        STATIC
        src/Order.cpp
        src/OrderBook.cpp
        src/MatchingEngine.cpp
        src/MatchingWorker.cpp
    )

    target_include_directories(
        matching_core_testing
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_compile_features(
        matching_core_testing
        PUBLIC
        cxx_std_20
    )

    set_target_properties(
        matching_core_testing
        PROPERTIES
        CXX_EXTENSIONS OFF
    )

    target_compile_definitions(
        matching_core_testing
        PUBLIC
        MATCHING_ENGINE_TESTING
    )

    if(MSVC)
        target_compile_options(
            matching_core_testing
            PRIVATE
            /W4
            /permissive-
        )
    else()
        target_compile_options(
            matching_core_testing
            PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()

    add_executable(
        order_tests
        tests/OrderTests.cpp
    )

    target_link_libraries(
        order_tests
        PRIVATE
        matching_core_testing
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
        matching_core_testing
    )

    add_test(
        NAME OrderBookTests
        COMMAND order_book_tests
    )

    add_executable(
        matching_engine_tests
        tests/MatchingEngineTests.cpp
    )

    target_link_libraries(
        matching_engine_tests
        PRIVATE
        matching_core_testing
    )

    add_test(
        NAME MatchingEngineTests
        COMMAND matching_engine_tests
    )

    add_executable(
        bounded_blocking_queue_tests
        tests/BoundedBlockingQueueTests.cpp
    )

    target_link_libraries(
        bounded_blocking_queue_tests
        PRIVATE
        matching_core_testing
    )

    add_test(
        NAME BoundedBlockingQueueTests
        COMMAND bounded_blocking_queue_tests
    )

    add_executable(
        matching_command_tests
        tests/MatchingCommandTests.cpp
    )

    target_link_libraries(
        matching_command_tests
        PRIVATE
        matching_core_testing
    )

    add_test(
        NAME MatchingCommandTests
        COMMAND matching_command_tests
    )

    add_executable(
        matching_worker_tests
        tests/MatchingWorkerTests.cpp
    )

    target_link_libraries(
        matching_worker_tests
        PRIVATE
        matching_core_testing
    )

    add_test(
        NAME MatchingWorkerTests
        COMMAND matching_worker_tests
    )
endif()
