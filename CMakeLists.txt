cmake_minimum_required(VERSION 3.20)

project(
    HFTMatchingEngine
    VERSION 0.1.0
    LANGUAGES CXX
)

add_executable(
    matching_engine
    src/main.cpp
)

target_include_directories(
    matching_engine
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(
    matching_engine
    PRIVATE
    cxx_std_20
)

set_target_properties(
    matching_engine
    PROPERTIES
    CXX_EXTENSIONS OFF
)

if(MSVC)
    target_compile_options(
        matching_engine
        PRIVATE
        /W4
        /permissive-
    )
else()
    target_compile_options(
        matching_engine
        PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()
