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

# Performance benchmarks
option(
    BUILD_BENCHMARKS
    "Build standalone performance benchmarks"
    OFF
)

if(BUILD_BENCHMARKS)
    add_executable(
        matching_benchmarks
        benchmarks/BenchmarkMain.cpp
    )

    target_link_libraries(
        matching_benchmarks
        PRIVATE
        matching_core
    )

    if(MSVC)
        target_compile_options(
            matching_benchmarks
            PRIVATE
            /W4
            /permissive-
        )
    else()
        target_compile_options(
            matching_benchmarks
            PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()

    # Optional instrumented benchmark configuration for queue backpressure measurement
    add_library(
        matching_core_instrumented
        STATIC
        src/Order.cpp
        src/OrderBook.cpp
        src/MatchingEngine.cpp
        src/MatchingWorker.cpp
    )

    target_include_directories(
        matching_core_instrumented
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_compile_features(
        matching_core_instrumented
        PUBLIC
        cxx_std_20
    )

    set_target_properties(
        matching_core_instrumented
        PROPERTIES
        CXX_EXTENSIONS OFF
    )

    target_compile_definitions(
        matching_core_instrumented
        PUBLIC
        MATCHING_ENGINE_QUEUE_INSTRUMENTATION
    )

    if(MSVC)
        target_compile_options(
            matching_core_instrumented
            PRIVATE
            /W4
            /permissive-
        )
    else()
        target_compile_options(
            matching_core_instrumented
            PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()

    add_executable(
        matching_benchmarks_instrumented
        benchmarks/BenchmarkMain.cpp
    )

    target_link_libraries(
        matching_benchmarks_instrumented
        PRIVATE
        matching_core_instrumented
    )

    if(MSVC)
        target_compile_options(
            matching_benchmarks_instrumented
            PRIVATE
            /W4
            /permissive-
        )
    else()
        target_compile_options(
            matching_benchmarks_instrumented
            PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()

    # P8-09: isolated profiling build
    if(WIN32)
        add_library(
            matching_core_profiling
            STATIC
            src/Order.cpp
            src/OrderBook.cpp
            src/MatchingEngine.cpp
            src/MatchingWorker.cpp
        )

        target_include_directories(
            matching_core_profiling
            PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        )

        target_compile_features(
            matching_core_profiling
            PUBLIC
            cxx_std_20
        )

        target_compile_definitions(
            matching_core_profiling
            PUBLIC
            MATCHING_ENGINE_PROFILING
        )

        if(MSVC)
            target_compile_options(
                matching_core_profiling
                PRIVATE
                /W4
                /permissive-
            )
        endif()
    endif()

    # P8-09: standalone engine memory profiling
    if(WIN32)
        add_executable(
            matching_allocation_profile
            benchmarks/AllocationProfile.cpp
        )

        target_link_libraries(
            matching_allocation_profile
            PRIVATE
            matching_core_profiling
            Psapi
        )

        if(MSVC)
            target_compile_options(
                matching_allocation_profile
                PRIVATE
                /W4
                /permissive-
            )
        endif()
    endif()
endif()

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
