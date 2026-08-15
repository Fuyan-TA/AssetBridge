cmake_minimum_required(VERSION 3.25)

project(AssetBridge VERSION 0.3.0 LANGUAGES CXX)

if(NOT MSVC)
    message(FATAL_ERROR "AssetBridge currently requires the MSVC toolchain.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(
    ASSETBRIDGE_PACKAGE_GUI_SMOKE
    "Run packaged GUI and 150% DPI smoke tests when Release package contents are verified"
    ON
)

set(ASSETBRIDGE_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/assetbridge_version.hpp.in"
    "${ASSETBRIDGE_GENERATED_INCLUDE_DIR}/assetbridge/version.hpp"
    @ONLY
)

add_library(assetbridge-version INTERFACE)
target_include_directories(assetbridge-version INTERFACE "${ASSETBRIDGE_GENERATED_INCLUDE_DIR}")

find_package(assimp CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)

add_library(assetbridge-product STATIC
    src/product/conversion_routes.cpp
    src/product/format_capabilities.cpp
    src/product/loss_preflight.cpp
)

target_include_directories(assetbridge-product
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(assetbridge-product PRIVATE /W4 /permissive- /utf-8)

add_library(assetbridge-batch STATIC
    src/batch/batch_coordinator.cpp
)

target_include_directories(assetbridge-batch
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(assetbridge-batch PUBLIC assetbridge-product)
target_compile_options(assetbridge-batch PRIVATE /W4 /permissive- /utf-8)

add_library(assetbridge-core STATIC
    src/core/asset_converter.cpp
    src/core/asset_inspector.cpp
    src/core/batch_processor.cpp
    src/core/batch_serializer.cpp
    src/core/companion_resolver.cpp
    src/core/conversion_serializer.cpp
    src/core/glb_container.cpp
    src/core/preflight_report.cpp
    src/core/preflight_serializer.cpp
    src/core/report_serializer.cpp
    src/core/runtime_capabilities.cpp
    src/core/scene_analysis.cpp
    src/core/texture_embedder.cpp
)

target_include_directories(assetbridge-core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(assetbridge-core
    PRIVATE
        assetbridge-product
        assetbridge-batch
        assimp::assimp
        nlohmann_json::nlohmann_json
)

target_compile_options(assetbridge-core PRIVATE /W4 /permissive- /utf-8)

add_library(assetbridge-desktop-model STATIC
    src/desktop/desktop_batch_state.cpp
    src/desktop/desktop_app_state.cpp
)

target_include_directories(assetbridge-desktop-model
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(assetbridge-desktop-model PUBLIC assetbridge-core)
target_compile_options(assetbridge-desktop-model PRIVATE /W4 /permissive- /utf-8)

add_executable(assetbridge-cli
    apps/assetbridge-cli/main.cpp
)

target_link_libraries(assetbridge-cli
    PRIVATE assetbridge-core assetbridge-batch assetbridge-version)
target_compile_options(assetbridge-cli PRIVATE /W4 /permissive- /utf-8)

add_executable(assetbridge-desktop WIN32
    apps/assetbridge-desktop/main.cpp
    src/platform/windows/windows_platform.cpp
)

target_link_libraries(assetbridge-desktop
    PRIVATE
        assetbridge-desktop-model
        imgui::imgui
        SDL3::SDL3
        d3d11
        dxgi
        ole32
        shell32
        assetbridge-version
)
target_compile_definitions(assetbridge-desktop PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(assetbridge-desktop PRIVATE /W4 /permissive- /utf-8)

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

add_executable(assetbridge-capability-tests
    tests/capability_matrix_tests.cpp
)

target_link_libraries(assetbridge-capability-tests PRIVATE assetbridge-product)
target_compile_options(assetbridge-capability-tests PRIVATE /W4 /permissive- /utf-8)

add_executable(assetbridge-conversion-route-tests
    tests/conversion_route_tests.cpp
)

target_link_libraries(assetbridge-conversion-route-tests PRIVATE assetbridge-product)
target_compile_options(assetbridge-conversion-route-tests PRIVATE /W4 /permissive- /utf-8)

add_executable(assetbridge-scene-semantics-tests
    tests/scene_semantics_tests.cpp
)

target_link_libraries(assetbridge-scene-semantics-tests
    PRIVATE
        assetbridge-core
        assimp::assimp
        nlohmann_json::nlohmann_json
)
target_compile_options(assetbridge-scene-semantics-tests PRIVATE /W4 /permissive- /utf-8)

add_executable(assetbridge-conversion-tests
    tests/conversion_tests.cpp
)

target_link_libraries(assetbridge-conversion-tests PRIVATE assetbridge-core assimp::assimp)
target_compile_options(assetbridge-conversion-tests PRIVATE /W4 /permissive- /utf-8)

add_executable(assetbridge-assimp-texture-probe-tests
    tests/assimp_texture_probe_tests.cpp
)

target_link_libraries(assetbridge-assimp-texture-probe-tests
    PRIVATE
        assimp::assimp
        nlohmann_json::nlohmann_json
)
target_compile_options(assetbridge-assimp-texture-probe-tests PRIVATE /W4 /permissive- /utf-8)

add_executable(assetbridge-companion-resolver-tests
    tests/companion_resolver_tests.cpp
)
target_link_libraries(assetbridge-companion-resolver-tests PRIVATE assetbridge-core)
target_compile_options(assetbridge-companion-resolver-tests PRIVATE /W4 /permissive- /utf-8)

add_executable(assetbridge-texture-pipeline-tests
    tests/texture_pipeline_tests.cpp
)
target_link_libraries(assetbridge-texture-pipeline-tests PRIVATE assetbridge-core assimp::assimp)
target_compile_options(assetbridge-texture-pipeline-tests PRIVATE /W4 /permissive- /utf-8)

add_executable(assetbridge-desktop-state-tests
    tests/desktop_app_state_tests.cpp
)

target_link_libraries(assetbridge-desktop-state-tests PRIVATE assetbridge-desktop-model)
target_compile_options(assetbridge-desktop-state-tests PRIVATE /W4 /permissive- /utf-8)

add_executable(assetbridge-desktop-batch-state-tests
    tests/desktop_batch_state_tests.cpp
)

target_link_libraries(assetbridge-desktop-batch-state-tests
    PRIVATE assetbridge-desktop-model assetbridge-batch)
target_compile_options(assetbridge-desktop-batch-state-tests PRIVATE /W4 /permissive- /utf-8)

add_executable(assetbridge-batch-coordinator-tests
    tests/batch_coordinator_tests.cpp
)

target_link_libraries(assetbridge-batch-coordinator-tests PRIVATE assetbridge-batch)
target_compile_options(assetbridge-batch-coordinator-tests PRIVATE /W4 /permissive- /utf-8)

add_executable(assetbridge-batch-conversion-tests
    tests/batch_conversion_tests.cpp
)

target_link_libraries(assetbridge-batch-conversion-tests
    PRIVATE
        assetbridge-core
        assetbridge-batch
        assetbridge-desktop-model
        nlohmann_json::nlohmann_json
)
target_compile_options(assetbridge-batch-conversion-tests PRIVATE /W4 /permissive- /utf-8)

add_test(NAME assetbridge-core-inspector COMMAND assetbridge-tests)
set_tests_properties(assetbridge-core-inspector PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_test(NAME assetbridge-capability-matrix COMMAND assetbridge-capability-tests)
add_test(NAME assetbridge-conversion-routes COMMAND assetbridge-conversion-route-tests)

add_test(
    NAME assetbridge-conversion-core
    COMMAND assetbridge-conversion-tests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets"
        "${CMAKE_CURRENT_BINARY_DIR}/conversion-core-output"
)

add_test(
    NAME assetbridge-assimp-texture-probe
    COMMAND assetbridge-assimp-texture-probe-tests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/textured/probe"
        "${CMAKE_CURRENT_BINARY_DIR}/assimp-texture-probe-output"
)
set_tests_properties(assetbridge-assimp-texture-probe PROPERTIES TIMEOUT 60)

add_test(
    NAME assetbridge-companion-resolver
    COMMAND assetbridge-companion-resolver-tests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/textured/probe"
        "${CMAKE_CURRENT_BINARY_DIR}/companion-resolver-output"
)
set_tests_properties(assetbridge-companion-resolver PROPERTIES TIMEOUT 60)

add_test(
    NAME assetbridge-texture-pipeline
    COMMAND assetbridge-texture-pipeline-tests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/textured/probe"
        "${CMAKE_CURRENT_BINARY_DIR}/texture-pipeline-output"
)
set_tests_properties(assetbridge-texture-pipeline PROPERTIES TIMEOUT 90)

add_test(NAME assetbridge-scene-semantics COMMAND assetbridge-scene-semantics-tests)
set_tests_properties(assetbridge-scene-semantics PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_test(NAME assetbridge-desktop-state COMMAND assetbridge-desktop-state-tests)
add_test(NAME assetbridge-desktop-batch-state COMMAND assetbridge-desktop-batch-state-tests)
add_test(NAME assetbridge-batch-coordinator COMMAND assetbridge-batch-coordinator-tests)
add_test(
    NAME assetbridge-batch-conversion
    COMMAND assetbridge-batch-conversion-tests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets"
        "${CMAKE_CURRENT_BINARY_DIR}/batch-conversion-output"
)
add_test(NAME assetbridge-desktop-smoke COMMAND assetbridge-desktop --smoke-test)
add_test(
    NAME assetbridge-desktop-smoke-150
    COMMAND assetbridge-desktop --smoke-test --ui-scale-150
)
set_tests_properties(assetbridge-desktop-smoke PROPERTIES TIMEOUT 30)
set_tests_properties(assetbridge-desktop-smoke-150 PROPERTIES TIMEOUT 30)

add_test(
    NAME assetbridge-desktop-acceptance-ascii
    COMMAND assetbridge-desktop
        --acceptance-input "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/minimal_triangle.obj"
        --acceptance-output "${CMAKE_CURRENT_BINARY_DIR}/desktop-acceptance-ascii"
)
add_test(
    NAME assetbridge-desktop-acceptance-unicode
    COMMAND assetbridge-desktop
        --acceptance-input "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/中文三角形.obj"
        --acceptance-output "${CMAKE_CURRENT_BINARY_DIR}/desktop-acceptance-unicode"
)
add_test(
    NAME assetbridge-desktop-acceptance-multimesh
    COMMAND assetbridge-desktop
        --acceptance-input "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/multi_two_triangles.obj"
        --acceptance-output "${CMAKE_CURRENT_BINARY_DIR}/desktop-acceptance-multimesh"
)
add_test(
    NAME assetbridge-desktop-reject-unverified
    COMMAND assetbridge-desktop
        --acceptance-input "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/unverified_texture.obj"
        --acceptance-output "${CMAKE_CURRENT_BINARY_DIR}/desktop-acceptance-unverified"
)
add_test(
    NAME assetbridge-desktop-acceptance-textured
    COMMAND assetbridge-desktop
        --acceptance-input "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/textured/probe/texture_behavior.obj"
        --acceptance-output "${CMAKE_CURRENT_BINARY_DIR}/desktop-acceptance-textured"
)
add_test(
    NAME assetbridge-desktop-acceptance-textured-unicode
    COMMAND assetbridge-desktop
        --acceptance-input "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/textured/probe/中文路径/中文材质.obj"
        --acceptance-output "${CMAKE_CURRENT_BINARY_DIR}/desktop-acceptance-textured-unicode"
)
set_tests_properties(
    assetbridge-desktop-acceptance-ascii
    assetbridge-desktop-acceptance-unicode
    assetbridge-desktop-acceptance-multimesh
    assetbridge-desktop-reject-unverified
    assetbridge-desktop-acceptance-textured
    assetbridge-desktop-acceptance-textured-unicode
    PROPERTIES TIMEOUT 30
)
set_tests_properties(assetbridge-desktop-reject-unverified PROPERTIES WILL_FAIL TRUE)

add_test(
    NAME assetbridge-desktop-batch-acceptance-ascii
    COMMAND assetbridge-desktop
        --acceptance-input "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/minimal_triangle.obj"
        --acceptance-input "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/multi_two_triangles.obj"
        --acceptance-output "${CMAKE_CURRENT_BINARY_DIR}/desktop-batch-acceptance-ascii"
)
add_test(
    NAME assetbridge-desktop-batch-acceptance-unicode
    COMMAND assetbridge-desktop
        --acceptance-input "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/中文三角形.obj"
        --acceptance-input "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/中文多网格.obj"
        --acceptance-output "${CMAKE_CURRENT_BINARY_DIR}/desktop-batch-acceptance-unicode"
)
add_test(
    NAME assetbridge-desktop-batch-acceptance-textured
    COMMAND assetbridge-desktop
        --acceptance-input "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/textured/probe/texture_behavior.obj"
        --acceptance-input "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/textured/verified/shared_texture.obj"
        --acceptance-output "${CMAKE_CURRENT_BINARY_DIR}/desktop-batch-acceptance-textured"
)
add_test(
    NAME assetbridge-desktop-batch-partial
    COMMAND assetbridge-desktop
        --acceptance-input "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/minimal_triangle.obj"
        --acceptance-input "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/textured/rejected/unsupported_semantic.obj"
        --acceptance-output "${CMAKE_CURRENT_BINARY_DIR}/desktop-batch-partial"
)
add_test(
    NAME assetbridge-desktop-batch-same-stem
    COMMAND assetbridge-desktop
        --acceptance-input "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/batch/same-a/same.obj"
        --acceptance-input "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/batch/same-b/same.obj"
        --acceptance-output "${CMAKE_CURRENT_BINARY_DIR}/desktop-batch-same-stem"
)
add_test(
    NAME assetbridge-desktop-batch-cancel-after-current
    COMMAND assetbridge-desktop
        --acceptance-input "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/minimal_triangle.obj"
        --acceptance-input "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/multi_two_triangles.obj"
        --acceptance-input "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/multi_triangle_quad.obj"
        --acceptance-input "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/textured/probe/texture_behavior.obj"
        --acceptance-input "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/textured/verified/shared_texture.obj"
        --acceptance-output "${CMAKE_CURRENT_BINARY_DIR}/desktop-batch-cancel-after-current"
        --acceptance-cancel-after-current
)
set_tests_properties(
    assetbridge-desktop-batch-acceptance-ascii
    assetbridge-desktop-batch-acceptance-unicode
    assetbridge-desktop-batch-acceptance-textured
    assetbridge-desktop-batch-partial
    assetbridge-desktop-batch-same-stem
    assetbridge-desktop-batch-cancel-after-current
    PROPERTIES TIMEOUT 45 LABELS "gui;acceptance"
)
set_tests_properties(assetbridge-desktop-batch-partial PROPERTIES WILL_FAIL TRUE)

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

add_assetbridge_cli_test(assetbridge-cli-preflight-stl-text
    "-DASSETBRIDGE_COMMAND=preflight"
    "-DASSETBRIDGE_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/minimal_triangle.obj"
    "-DASSETBRIDGE_TARGET=stl"
    "-DASSETBRIDGE_EXPECTED_EXIT=0"
    "-DASSETBRIDGE_EXPECTED_STDOUT=Target Format: stl|has_node_hierarchy: false|normals_partial|uv0_unsupported|material_slots_unsupported|Compatibility Result: lossy|Overall Result: lossy"
    "-DASSETBRIDGE_UNEXPECTED_STDOUT=node_hierarchy_unsupported"
)

add_assetbridge_cli_test(assetbridge-cli-preflight-stl-json
    "-DASSETBRIDGE_COMMAND=preflight"
    "-DASSETBRIDGE_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/minimal_triangle.obj"
    "-DASSETBRIDGE_TARGET=STL"
    "-DASSETBRIDGE_OPTION=--json"
    "-DASSETBRIDGE_EXPECTED_EXIT=0"
    "-DASSETBRIDGE_VALIDATE_JSON=preflight_stl"
    "-DASSETBRIDGE_COMPARE_TEXT=TRUE"
)

add_assetbridge_cli_test(assetbridge-cli-preflight-unicode-glb-json
    "-DASSETBRIDGE_COMMAND=preflight"
    "-DASSETBRIDGE_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/中文三角形.obj"
    "-DASSETBRIDGE_TARGET=glb"
    "-DASSETBRIDGE_OPTION=--json"
    "-DASSETBRIDGE_EXPECTED_EXIT=0"
    "-DASSETBRIDGE_VALIDATE_JSON=preflight_glb"
    "-DASSETBRIDGE_COMPARE_TEXT=TRUE"
)

add_assetbridge_cli_test(assetbridge-cli-preflight-multimesh-glb-json
    "-DASSETBRIDGE_COMMAND=preflight"
    "-DASSETBRIDGE_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/multi_two_triangles.obj"
    "-DASSETBRIDGE_TARGET=glb"
    "-DASSETBRIDGE_OPTION=--json"
    "-DASSETBRIDGE_EXPECTED_EXIT=0"
    "-DASSETBRIDGE_VALIDATE_JSON=preflight_glb_multi"
    "-DASSETBRIDGE_COMPARE_TEXT=TRUE"
)

add_assetbridge_cli_test(assetbridge-cli-preflight-textured-glb-json
    "-DASSETBRIDGE_COMMAND=preflight"
    "-DASSETBRIDGE_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/textured/probe/texture_behavior.obj"
    "-DASSETBRIDGE_TARGET=glb"
    "-DASSETBRIDGE_OPTION=--json"
    "-DASSETBRIDGE_EXPECTED_EXIT=0"
    "-DASSETBRIDGE_VALIDATE_JSON=preflight_glb_multi"
    "-DASSETBRIDGE_COMPARE_TEXT=TRUE"
)

add_assetbridge_cli_test(assetbridge-cli-preflight-unknown-json
    "-DASSETBRIDGE_COMMAND=preflight"
    "-DASSETBRIDGE_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/minimal_triangle.obj"
    "-DASSETBRIDGE_TARGET=fbx"
    "-DASSETBRIDGE_OPTION=--json"
    "-DASSETBRIDGE_EXPECTED_EXIT=2"
    "-DASSETBRIDGE_VALIDATE_JSON=preflight_unknown"
)

add_assetbridge_cli_test(assetbridge-cli-convert-ascii-json
    "-DASSETBRIDGE_COMMAND=convert"
    "-DASSETBRIDGE_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/minimal_triangle.obj"
    "-DASSETBRIDGE_TARGET=GLB"
    "-DASSETBRIDGE_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cli-convert-ascii"
    "-DASSETBRIDGE_CLEAN_OUTPUT=TRUE"
    "-DASSETBRIDGE_OPTION=--json"
    "-DASSETBRIDGE_EXPECTED_EXIT=0"
    "-DASSETBRIDGE_VALIDATE_JSON=conversion_success"
)

add_assetbridge_cli_test(assetbridge-cli-convert-unicode-json
    "-DASSETBRIDGE_COMMAND=convert"
    "-DASSETBRIDGE_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/中文三角形.obj"
    "-DASSETBRIDGE_TARGET=glb2"
    "-DASSETBRIDGE_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cli-convert-unicode"
    "-DASSETBRIDGE_CLEAN_OUTPUT=TRUE"
    "-DASSETBRIDGE_OPTION=--json"
    "-DASSETBRIDGE_EXPECTED_EXIT=0"
    "-DASSETBRIDGE_VALIDATE_JSON=conversion_success"
)

add_assetbridge_cli_test(assetbridge-cli-convert-multimesh-json
    "-DASSETBRIDGE_COMMAND=convert"
    "-DASSETBRIDGE_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/multi_two_triangles.obj"
    "-DASSETBRIDGE_TARGET=glb"
    "-DASSETBRIDGE_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cli-convert-multimesh"
    "-DASSETBRIDGE_CLEAN_OUTPUT=TRUE"
    "-DASSETBRIDGE_OPTION=--json"
    "-DASSETBRIDGE_EXPECTED_EXIT=0"
    "-DASSETBRIDGE_VALIDATE_JSON=conversion_success"
)

add_assetbridge_cli_test(assetbridge-cli-convert-multimesh-quad-json
    "-DASSETBRIDGE_COMMAND=convert"
    "-DASSETBRIDGE_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/multi_triangle_quad.obj"
    "-DASSETBRIDGE_TARGET=glb2"
    "-DASSETBRIDGE_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cli-convert-multimesh-quad"
    "-DASSETBRIDGE_CLEAN_OUTPUT=TRUE"
    "-DASSETBRIDGE_OPTION=--json"
    "-DASSETBRIDGE_EXPECTED_EXIT=0"
    "-DASSETBRIDGE_VALIDATE_JSON=conversion_success"
)

add_assetbridge_cli_test(assetbridge-cli-convert-multimesh-unicode-json
    "-DASSETBRIDGE_COMMAND=convert"
    "-DASSETBRIDGE_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/中文多网格.obj"
    "-DASSETBRIDGE_TARGET=GLB"
    "-DASSETBRIDGE_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cli-convert-multimesh-unicode"
    "-DASSETBRIDGE_CLEAN_OUTPUT=TRUE"
    "-DASSETBRIDGE_OPTION=--json"
    "-DASSETBRIDGE_EXPECTED_EXIT=0"
    "-DASSETBRIDGE_VALIDATE_JSON=conversion_success"
)

add_assetbridge_cli_test(assetbridge-cli-convert-textured-json
    "-DASSETBRIDGE_COMMAND=convert"
    "-DASSETBRIDGE_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/textured/probe/texture_behavior.obj"
    "-DASSETBRIDGE_TARGET=glb"
    "-DASSETBRIDGE_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cli-convert-textured"
    "-DASSETBRIDGE_CLEAN_OUTPUT=TRUE"
    "-DASSETBRIDGE_OPTION=--json"
    "-DASSETBRIDGE_EXPECTED_EXIT=0"
    "-DASSETBRIDGE_VALIDATE_JSON=conversion_success"
)

add_assetbridge_cli_test(assetbridge-cli-convert-textured-unicode-json
    "-DASSETBRIDGE_COMMAND=convert"
    "-DASSETBRIDGE_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/textured/probe/中文路径/中文材质.obj"
    "-DASSETBRIDGE_TARGET=glb"
    "-DASSETBRIDGE_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cli-convert-textured-unicode"
    "-DASSETBRIDGE_CLEAN_OUTPUT=TRUE"
    "-DASSETBRIDGE_OPTION=--json"
    "-DASSETBRIDGE_EXPECTED_EXIT=0"
    "-DASSETBRIDGE_VALIDATE_JSON=conversion_success"
)

add_assetbridge_cli_test(assetbridge-cli-convert-shared-texture-json
    "-DASSETBRIDGE_COMMAND=convert"
    "-DASSETBRIDGE_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/textured/verified/shared_texture.obj"
    "-DASSETBRIDGE_TARGET=glb"
    "-DASSETBRIDGE_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cli-convert-shared-texture"
    "-DASSETBRIDGE_CLEAN_OUTPUT=TRUE"
    "-DASSETBRIDGE_OPTION=--json"
    "-DASSETBRIDGE_EXPECTED_EXIT=0"
    "-DASSETBRIDGE_VALIDATE_JSON=conversion_success"
)

add_assetbridge_cli_test(assetbridge-cli-convert-empty-mesh-json
    "-DASSETBRIDGE_COMMAND=convert"
    "-DASSETBRIDGE_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/empty_mesh.obj"
    "-DASSETBRIDGE_TARGET=glb"
    "-DASSETBRIDGE_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cli-convert-empty-mesh"
    "-DASSETBRIDGE_CLEAN_OUTPUT=TRUE"
    "-DASSETBRIDGE_EXPECT_EMPTY_OUTPUT=TRUE"
    "-DASSETBRIDGE_OPTION=--json"
    "-DASSETBRIDGE_EXPECTED_EXIT=1"
    "-DASSETBRIDGE_VALIDATE_JSON=conversion_import_error"
)

add_assetbridge_cli_test(assetbridge-cli-convert-invalid-json
    "-DASSETBRIDGE_COMMAND=convert"
    "-DASSETBRIDGE_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/invalid.obj"
    "-DASSETBRIDGE_TARGET=glb"
    "-DASSETBRIDGE_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cli-convert-invalid"
    "-DASSETBRIDGE_CLEAN_OUTPUT=TRUE"
    "-DASSETBRIDGE_EXPECT_EMPTY_OUTPUT=TRUE"
    "-DASSETBRIDGE_OPTION=--json"
    "-DASSETBRIDGE_EXPECTED_EXIT=1"
    "-DASSETBRIDGE_VALIDATE_JSON=conversion_import_error"
)

add_assetbridge_cli_test(assetbridge-cli-convert-unverified-json
    "-DASSETBRIDGE_COMMAND=convert"
    "-DASSETBRIDGE_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/unverified_texture.obj"
    "-DASSETBRIDGE_TARGET=glb"
    "-DASSETBRIDGE_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cli-convert-unverified"
    "-DASSETBRIDGE_CLEAN_OUTPUT=TRUE"
    "-DASSETBRIDGE_EXPECT_EMPTY_OUTPUT=TRUE"
    "-DASSETBRIDGE_OPTION=--json"
    "-DASSETBRIDGE_EXPECTED_EXIT=1"
    "-DASSETBRIDGE_VALIDATE_JSON=conversion_unverified"
)

add_assetbridge_cli_test(assetbridge-cli-convert-path-traversal-json
    "-DASSETBRIDGE_COMMAND=convert"
    "-DASSETBRIDGE_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/textured/rejected/path_traversal.obj"
    "-DASSETBRIDGE_TARGET=glb"
    "-DASSETBRIDGE_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cli-convert-path-traversal"
    "-DASSETBRIDGE_CLEAN_OUTPUT=TRUE"
    "-DASSETBRIDGE_EXPECT_EMPTY_OUTPUT=TRUE"
    "-DASSETBRIDGE_OPTION=--json"
    "-DASSETBRIDGE_EXPECTED_EXIT=1"
    "-DASSETBRIDGE_VALIDATE_JSON=conversion_unverified"
)

add_assetbridge_cli_test(assetbridge-cli-convert-unsupported-semantic-json
    "-DASSETBRIDGE_COMMAND=convert"
    "-DASSETBRIDGE_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/textured/rejected/unsupported_semantic.obj"
    "-DASSETBRIDGE_TARGET=glb"
    "-DASSETBRIDGE_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cli-convert-unsupported-semantic"
    "-DASSETBRIDGE_CLEAN_OUTPUT=TRUE"
    "-DASSETBRIDGE_EXPECT_EMPTY_OUTPUT=TRUE"
    "-DASSETBRIDGE_OPTION=--json"
    "-DASSETBRIDGE_EXPECTED_EXIT=1"
    "-DASSETBRIDGE_VALIDATE_JSON=conversion_unverified"
)

add_assetbridge_cli_test(assetbridge-cli-convert-unknown-target-json
    "-DASSETBRIDGE_COMMAND=convert"
    "-DASSETBRIDGE_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/minimal_triangle.obj"
    "-DASSETBRIDGE_TARGET=fbx"
    "-DASSETBRIDGE_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cli-convert-unknown"
    "-DASSETBRIDGE_CLEAN_OUTPUT=TRUE"
    "-DASSETBRIDGE_EXPECT_EMPTY_OUTPUT=TRUE"
    "-DASSETBRIDGE_OPTION=--json"
    "-DASSETBRIDGE_EXPECTED_EXIT=2"
    "-DASSETBRIDGE_VALIDATE_JSON=conversion_unknown"
)

add_assetbridge_cli_test(assetbridge-cli-batch-text-success
    "-DASSETBRIDGE_COMMAND=batch"
    "-DASSETBRIDGE_BATCH_INPUTS=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/minimal_triangle.obj|${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/中文三角形.obj"
    "-DASSETBRIDGE_TARGET=glb"
    "-DASSETBRIDGE_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cli-batch-text-success"
    "-DASSETBRIDGE_CLEAN_OUTPUT=TRUE"
    "-DASSETBRIDGE_EXPECTED_EXIT=0"
    "-DASSETBRIDGE_EXPECTED_STDOUT=Batch Status: success|Total: 2|Succeeded: 2|job-000001|job-000002"
)

add_assetbridge_cli_test(assetbridge-cli-batch-json-success
    "-DASSETBRIDGE_COMMAND=batch"
    "-DASSETBRIDGE_BATCH_INPUTS=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/minimal_triangle.obj|${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/中文三角形.obj"
    "-DASSETBRIDGE_TARGET=GLB2"
    "-DASSETBRIDGE_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cli-batch-json-success"
    "-DASSETBRIDGE_CLEAN_OUTPUT=TRUE"
    "-DASSETBRIDGE_OPTION=--json"
    "-DASSETBRIDGE_EXPECTED_EXIT=0"
    "-DASSETBRIDGE_VALIDATE_JSON=batch_success"
    "-DASSETBRIDGE_EXPECTED_BATCH_STATUS=success"
)

add_assetbridge_cli_test(assetbridge-cli-batch-json-partial
    "-DASSETBRIDGE_COMMAND=batch"
    "-DASSETBRIDGE_BATCH_INPUTS=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/minimal_triangle.obj|${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/invalid.obj|${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/textured/rejected/unsupported_semantic.obj"
    "-DASSETBRIDGE_TARGET=glb"
    "-DASSETBRIDGE_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cli-batch-json-partial"
    "-DASSETBRIDGE_CLEAN_OUTPUT=TRUE"
    "-DASSETBRIDGE_OPTION=--json"
    "-DASSETBRIDGE_EXPECTED_EXIT=1"
    "-DASSETBRIDGE_VALIDATE_JSON=batch_partial"
    "-DASSETBRIDGE_EXPECTED_BATCH_STATUS=partial"
)

add_assetbridge_cli_test(assetbridge-cli-batch-same-stem-json
    "-DASSETBRIDGE_COMMAND=batch"
    "-DASSETBRIDGE_BATCH_INPUTS=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/batch/same-a/same.obj|${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/batch/same-b/same.obj"
    "-DASSETBRIDGE_TARGET=glb"
    "-DASSETBRIDGE_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cli-batch-same-stem"
    "-DASSETBRIDGE_CLEAN_OUTPUT=TRUE"
    "-DASSETBRIDGE_OPTION=--json"
    "-DASSETBRIDGE_EXPECTED_EXIT=0"
    "-DASSETBRIDGE_VALIDATE_JSON=batch_success"
    "-DASSETBRIDGE_EXPECTED_BATCH_STATUS=success"
)

add_assetbridge_cli_test(assetbridge-cli-batch-no-input-json
    "-DASSETBRIDGE_COMMAND=batch"
    "-DASSETBRIDGE_TARGET=glb"
    "-DASSETBRIDGE_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cli-batch-no-input"
    "-DASSETBRIDGE_CLEAN_OUTPUT=TRUE"
    "-DASSETBRIDGE_EXPECT_EMPTY_OUTPUT=TRUE"
    "-DASSETBRIDGE_OPTION=--json"
    "-DASSETBRIDGE_EXPECTED_EXIT=2"
    "-DASSETBRIDGE_VALIDATE_JSON=batch_argument_error"
)

add_assetbridge_cli_test(assetbridge-cli-batch-non-obj
    "-DASSETBRIDGE_COMMAND=batch"
    "-DASSETBRIDGE_BATCH_INPUTS=${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/not-an-obj.glb"
    "-DASSETBRIDGE_TARGET=glb"
    "-DASSETBRIDGE_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cli-batch-non-obj"
    "-DASSETBRIDGE_CLEAN_OUTPUT=TRUE"
    "-DASSETBRIDGE_EXPECT_EMPTY_OUTPUT=TRUE"
    "-DASSETBRIDGE_EXPECTED_EXIT=2"
    "-DASSETBRIDGE_EXPECTED_STDERR=not_obj|no_valid_input"
)

add_test(
    NAME assetbridge-cli-version
    COMMAND assetbridge-cli --version
)
set_tests_properties(assetbridge-cli-version PROPERTIES
    PASS_REGULAR_EXPRESSION "^AssetBridge ${PROJECT_VERSION}"
)

set_tests_properties(
    assetbridge-core-inspector
    assetbridge-capability-matrix
    assetbridge-conversion-routes
    assetbridge-conversion-core
    assetbridge-assimp-texture-probe
    assetbridge-companion-resolver
    assetbridge-texture-pipeline
    assetbridge-scene-semantics
    assetbridge-batch-coordinator
    assetbridge-batch-conversion
    PROPERTIES LABELS "unit"
)
set_tests_properties(assetbridge-desktop-state PROPERTIES LABELS "unit;desktop-state")
set_tests_properties(assetbridge-desktop-batch-state PROPERTIES LABELS "unit;desktop-state")
set_tests_properties(
    assetbridge-desktop-smoke
    assetbridge-desktop-smoke-150
    PROPERTIES LABELS "gui"
)
set_tests_properties(
    assetbridge-desktop-acceptance-ascii
    assetbridge-desktop-acceptance-unicode
    assetbridge-desktop-acceptance-multimesh
    assetbridge-desktop-reject-unverified
    assetbridge-desktop-acceptance-textured
    assetbridge-desktop-acceptance-textured-unicode
    PROPERTIES LABELS "gui;acceptance"
)

get_property(ASSETBRIDGE_REGISTERED_TESTS DIRECTORY PROPERTY TESTS)
foreach(ASSETBRIDGE_TEST IN LISTS ASSETBRIDGE_REGISTERED_TESTS)
    if(ASSETBRIDGE_TEST MATCHES "^assetbridge-cli-")
        set_property(TEST "${ASSETBRIDGE_TEST}" APPEND PROPERTY LABELS "cli")
        set_property(TEST "${ASSETBRIDGE_TEST}" PROPERTY TIMEOUT 60)
    endif()
    if(ASSETBRIDGE_TEST MATCHES "^assetbridge-cli-(convert|batch)-")
        set_property(TEST "${ASSETBRIDGE_TEST}" APPEND PROPERTY LABELS "acceptance")
    endif()
endforeach()

set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ".")
set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_NO_WARNINGS ON)
set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP ON)
include(InstallRequiredSystemLibraries)

set(ASSETBRIDGE_RUNTIME_SEARCH_DIRECTORIES)
foreach(ASSETBRIDGE_RUNTIME_LIBRARY IN LISTS CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
    get_filename_component(ASSETBRIDGE_RUNTIME_DIRECTORY
        "${ASSETBRIDGE_RUNTIME_LIBRARY}" DIRECTORY)
    list(APPEND ASSETBRIDGE_RUNTIME_SEARCH_DIRECTORIES
        "${ASSETBRIDGE_RUNTIME_DIRECTORY}")
endforeach()
list(REMOVE_DUPLICATES ASSETBRIDGE_RUNTIME_SEARCH_DIRECTORIES)

set(ASSETBRIDGE_REQUIRED_MSVC_RUNTIME_LIBS)
foreach(ASSETBRIDGE_RUNTIME_LIBRARY IN LISTS CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
    get_filename_component(ASSETBRIDGE_RUNTIME_NAME
        "${ASSETBRIDGE_RUNTIME_LIBRARY}" NAME)
    string(TOLOWER "${ASSETBRIDGE_RUNTIME_NAME}" ASSETBRIDGE_RUNTIME_NAME_LOWER)
    if(ASSETBRIDGE_RUNTIME_NAME_LOWER MATCHES
        "^(msvcp140|msvcp140_atomic_wait|vcruntime140|vcruntime140_1)\\.dll$")
        list(APPEND ASSETBRIDGE_REQUIRED_MSVC_RUNTIME_LIBS
            "${ASSETBRIDGE_RUNTIME_LIBRARY}")
    endif()
endforeach()
if(NOT ASSETBRIDGE_REQUIRED_MSVC_RUNTIME_LIBS)
    message(FATAL_ERROR "Required MSVC redistributable libraries were not discovered.")
endif()

install(
    TARGETS assetbridge-cli assetbridge-desktop
    RUNTIME_DEPENDENCY_SET assetbridge-runtime-dependencies
    RUNTIME DESTINATION "."
)
install(
    RUNTIME_DEPENDENCY_SET assetbridge-runtime-dependencies
    DESTINATION "."
    DIRECTORIES
        "$<TARGET_FILE_DIR:assimp::assimp>"
        "$<TARGET_FILE_DIR:SDL3::SDL3>"
        ${ASSETBRIDGE_RUNTIME_SEARCH_DIRECTORIES}
    PRE_EXCLUDE_REGEXES
        "api-ms-.*"
        "ext-ms-.*"
        "msvcp140.*\\.dll"
        "vcruntime140.*\\.dll"
    POST_EXCLUDE_REGEXES
        ".*[\\\\/]system32[\\\\/].*"
)
install(PROGRAMS ${ASSETBRIDGE_REQUIRED_MSVC_RUNTIME_LIBS} DESTINATION ".")
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
    DESTINATION "."
)
install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/licenses/"
    DESTINATION "third_party/licenses"
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_test(
        NAME assetbridge-package-contents
        COMMAND "${CMAKE_COMMAND}"
            "-DASSETBRIDGE_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            "-DASSETBRIDGE_STAGING_DIR=${CMAKE_CURRENT_BINARY_DIR}/package-test-staging"
            "-DASSETBRIDGE_EXPECTED_VERSION=${PROJECT_VERSION}"
            "-DASSETBRIDGE_RUN_GUI_SMOKE=${ASSETBRIDGE_PACKAGE_GUI_SMOKE}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_package.cmake"
    )
    set_tests_properties(assetbridge-package-contents PROPERTIES
        LABELS "acceptance;package"
        TIMEOUT 60
    )
endif()

set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "AssetBridge")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "AssetBridge-v${PROJECT_VERSION}-win-x64")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/packages")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_PACKAGE_CHECKSUM "SHA256")
include(CPack)
