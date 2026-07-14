cmake_minimum_required(VERSION 3.25)

project(AssetBridge VERSION 0.1.0 LANGUAGES CXX)

if(NOT MSVC)
    message(FATAL_ERROR "AssetBridge Phase 1A requires the MSVC toolchain.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(assimp CONFIG REQUIRED)

add_library(assetbridge-core STATIC
    src/core/asset_inspector.cpp
)

target_include_directories(assetbridge-core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(assetbridge-core
    PRIVATE
        assimp::assimp
)

target_compile_options(assetbridge-core PRIVATE /W4 /permissive- /utf-8)

add_executable(assetbridge-cli
    apps/assetbridge-cli/main.cpp
)

target_link_libraries(assetbridge-cli PRIVATE assetbridge-core)
target_compile_options(assetbridge-cli PRIVATE /W4 /permissive- /utf-8)

enable_testing()

add_executable(assetbridge-tests
    tests/asset_inspector_tests.cpp
)

target_link_libraries(assetbridge-tests PRIVATE assetbridge-core)
target_compile_options(assetbridge-tests PRIVATE /W4 /permissive- /utf-8)

add_test(NAME assetbridge-inspect-obj COMMAND assetbridge-tests)
set_tests_properties(assetbridge-inspect-obj PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
