cmake_minimum_required(VERSION 3.25)

project(AssetBridge VERSION 0.1.0 LANGUAGES CXX)

if(NOT MSVC)
    message(FATAL_ERROR "AssetBridge currently requires the MSVC toolchain.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(assimp CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(assetbridge-core STATIC
    src/core/asset_inspector.cpp
    src/core/report_serializer.cpp
    src/core/runtime_capabilities.cpp
)

target_include_directories(assetbridge-core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(assetbridge-core
    PRIVATE
        assimp::assimp
        nlohmann_json::nlohmann_json
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

target_link_libraries(assetbridge-tests PRIVATE assetbridge-core assimp::assimp)
target_compile_options(assetbridge-tests PRIVATE /W4 /permissive- /utf-8)

add_executable(assetbridge-json-validator
    tests/json_validator.cpp
)

target_link_libraries(assetbridge-json-validator PRIVATE nlohmann_json::nlohmann_json)
target_compile_options(assetbridge-json-validator PRIVATE /W4 /permissive- /utf-8)

add_test(NAME assetbridge-core-inspector COMMAND assetbridge-tests)
set_tests_properties(assetbridge-core-inspector PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

function(add_assetbridge_cli_test test_name)
    add_test(
        NAME "${test_name}"
        COMMAND "${CMAKE_COMMAND}"
            "-DASSETBRIDGE_CLI=$<TARGET_FILE:assetbridge-cli>"
            "-DASSETBRIDGE_JSON_VALIDATOR=$<TARGET_FILE:assetbridge-json-validator>"
            "-DASSETBRIDGE_JSON_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${test_name}.json"
            ${ARGN}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_cli_test.cmake"
    )
endfunction()

add_assetbridge_cli_test(assetbridge-cli-inspect-ascii
    "-DASSETBRIDGE_COMMAND=inspect"
    "-DASSETBRIDGE_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/minimal_triangle.obj"
    "-DASSETBRIDGE_EXPECTED_EXIT=0"
    "-DASSETBRIDGE_EXPECTED_STDOUT=Meshes: 1|Vertices: 3|Triangles: 1|Has Normals: yes"
)

add_assetbridge_cli_test(assetbridge-cli-inspect-unicode
    "-DASSETBRIDGE_COMMAND=inspect"
    "-DASSETBRIDGE_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/中文三角形.obj"
    "-DASSETBRIDGE_EXPECTED_EXIT=0"
    "-DASSETBRIDGE_EXPECTED_STDOUT=中文三角形.obj|Meshes: 1|Vertices: 3|Triangles: 1|Has Normals: yes"
)

add_assetbridge_cli_test(assetbridge-cli-missing-file
    "-DASSETBRIDGE_COMMAND=inspect"
    "-DASSETBRIDGE_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/does_not_exist.obj"
    "-DASSETBRIDGE_EXPECTED_EXIT=1"
    "-DASSETBRIDGE_EXPECTED_STDERR=Error [file_not_found]:"
)

add_assetbridge_cli_test(assetbridge-cli-invalid-obj
    "-DASSETBRIDGE_COMMAND=inspect"
    "-DASSETBRIDGE_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/invalid.obj"
    "-DASSETBRIDGE_EXPECTED_EXIT=1"
    "-DASSETBRIDGE_EXPECTED_STDERR_ANY=Error [import_failed]:|Error [invalid_scene]:"
)

add_assetbridge_cli_test(assetbridge-cli-invalid-arguments
    "-DASSETBRIDGE_COMMAND=inspect"
    "-DASSETBRIDGE_EXPECTED_EXIT=2"
    "-DASSETBRIDGE_EXPECTED_STDERR=Usage:|assetbridge-cli inspect <file> [--json]"
)

add_assetbridge_cli_test(assetbridge-cli-inspect-ascii-json
    "-DASSETBRIDGE_COMMAND=inspect"
    "-DASSETBRIDGE_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/minimal_triangle.obj"
    "-DASSETBRIDGE_OPTION=--json"
    "-DASSETBRIDGE_EXPECTED_EXIT=0"
    "-DASSETBRIDGE_VALIDATE_JSON=inspect_success"
)

add_assetbridge_cli_test(assetbridge-cli-inspect-unicode-json
    "-DASSETBRIDGE_COMMAND=inspect"
    "-DASSETBRIDGE_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/中文三角形.obj"
    "-DASSETBRIDGE_OPTION=--json"
    "-DASSETBRIDGE_EXPECTED_EXIT=0"
    "-DASSETBRIDGE_VALIDATE_JSON=inspect_success"
)

add_assetbridge_cli_test(assetbridge-cli-missing-file-json
    "-DASSETBRIDGE_COMMAND=inspect"
    "-DASSETBRIDGE_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/does_not_exist.obj"
    "-DASSETBRIDGE_OPTION=--json"
    "-DASSETBRIDGE_EXPECTED_EXIT=1"
    "-DASSETBRIDGE_VALIDATE_JSON=inspect_error"
)

add_assetbridge_cli_test(assetbridge-cli-capabilities-text
    "-DASSETBRIDGE_COMMAND=capabilities"
    "-DASSETBRIDGE_EXPECTED_EXIT=0"
    "-DASSETBRIDGE_EXPECTED_STDOUT=Assimp Version:|Importers:|.obj | runtime_available=true | product_enabled=true | verified=true|Exporters:|product_enabled=false | verified=false"
)

add_assetbridge_cli_test(assetbridge-cli-capabilities-json
    "-DASSETBRIDGE_COMMAND=capabilities"
    "-DASSETBRIDGE_OPTION=--json"
    "-DASSETBRIDGE_EXPECTED_EXIT=0"
    "-DASSETBRIDGE_VALIDATE_JSON=capabilities"
)
