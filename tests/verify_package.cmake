cmake_minimum_required(VERSION 3.25)

foreach(required_variable IN ITEMS
    ASSETBRIDGE_BUILD_DIR
    ASSETBRIDGE_STAGING_DIR
    ASSETBRIDGE_EXPECTED_VERSION
    ASSETBRIDGE_RUN_GUI_SMOKE)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "Missing required variable: ${required_variable}")
    endif()
endforeach()

file(REMOVE_RECURSE "${ASSETBRIDGE_STAGING_DIR}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${ASSETBRIDGE_BUILD_DIR}"
        --prefix "${ASSETBRIDGE_STAGING_DIR}"
    RESULT_VARIABLE install_result
    OUTPUT_VARIABLE install_stdout
    ERROR_VARIABLE install_stderr
)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR
        "Package staging install failed (${install_result}).\n"
        "stdout:\n${install_stdout}\n"
        "stderr:\n${install_stderr}")
endif()

set(required_files
    assetbridge-cli.exe
    assetbridge-desktop.exe
    assimp-vc143-mt.dll
    SDL3.dll
    poly2tri.dll
    minizip.dll
    z.dll
    kubazip.dll
    pugixml.dll
    msvcp140.dll
    msvcp140_atomic_wait.dll
    vcruntime140.dll
    vcruntime140_1.dll
    LICENSE
    THIRD_PARTY_NOTICES.md
    third_party/licenses/assimp.txt
    third_party/licenses/imgui.txt
    third_party/licenses/jhasse-poly2tri.txt
    third_party/licenses/kubazip.txt
    third_party/licenses/minizip.txt
    third_party/licenses/nlohmann-json.txt
    third_party/licenses/pugixml.txt
    third_party/licenses/sdl3.txt
    third_party/licenses/zlib.txt
)
foreach(required_file IN LISTS required_files)
    if(NOT EXISTS "${ASSETBRIDGE_STAGING_DIR}/${required_file}")
        message(FATAL_ERROR "Required package file is missing: ${required_file}")
    endif()
endforeach()

file(GLOB_RECURSE package_files LIST_DIRECTORIES FALSE "${ASSETBRIDGE_STAGING_DIR}/*")
foreach(package_file IN LISTS package_files)
    get_filename_component(package_name "${package_file}" NAME)
    string(TOLOWER "${package_name}" package_name_lower)
    if(package_name_lower MATCHES "\\.(pdb|obj|mtl|cpp|cxx|h|hpp|lib|exp|ilk|log)$")
        message(FATAL_ERROR "Forbidden package file: ${package_file}")
    endif()
    if(package_name_lower MATCHES "test.*\\.exe$")
        message(FATAL_ERROR "Test executable leaked into package: ${package_file}")
    endif()
endforeach()

file(GLOB_RECURSE package_entries LIST_DIRECTORIES TRUE "${ASSETBRIDGE_STAGING_DIR}/*")
foreach(package_entry IN LISTS package_entries)
    if(IS_DIRECTORY "${package_entry}")
        get_filename_component(package_directory_name "${package_entry}" NAME)
        if(package_directory_name MATCHES "^\\.assetbridge-tmp-")
            message(FATAL_ERROR
                "Temporary conversion directory leaked into package: ${package_entry}")
        endif()
    endif()
endforeach()

execute_process(
    COMMAND "${ASSETBRIDGE_STAGING_DIR}/assetbridge-cli.exe" --version
    RESULT_VARIABLE version_result
    OUTPUT_VARIABLE version_stdout
    ERROR_VARIABLE version_stderr
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT version_result EQUAL 0
    OR NOT version_stdout STREQUAL "AssetBridge ${ASSETBRIDGE_EXPECTED_VERSION}")
    message(FATAL_ERROR
        "Packaged CLI version check failed. exit=${version_result}, "
        "stdout='${version_stdout}', stderr='${version_stderr}'")
endif()

execute_process(
    COMMAND "${ASSETBRIDGE_STAGING_DIR}/assetbridge-cli.exe" capabilities --json
    RESULT_VARIABLE cli_smoke_result
    OUTPUT_VARIABLE cli_smoke_stdout
    ERROR_VARIABLE cli_smoke_stderr
)
if(NOT cli_smoke_result EQUAL 0 OR NOT cli_smoke_stdout MATCHES "assetbridge.capabilities.v1")
    message(FATAL_ERROR
        "Packaged CLI smoke failed. exit=${cli_smoke_result}\n${cli_smoke_stderr}")
endif()

if(ASSETBRIDGE_RUN_GUI_SMOKE)
    foreach(gui_arguments IN ITEMS "--smoke-test" "--smoke-test;--ui-scale-150")
        execute_process(
            COMMAND "${ASSETBRIDGE_STAGING_DIR}/assetbridge-desktop.exe" ${gui_arguments}
            RESULT_VARIABLE gui_smoke_result
        )
        if(NOT gui_smoke_result EQUAL 0)
            message(FATAL_ERROR
                "Packaged GUI smoke failed for '${gui_arguments}' with exit ${gui_smoke_result}")
        endif()
    endforeach()
else()
    message(STATUS
        "Packaged GUI/DX11 smoke tests were not executed because "
        "ASSETBRIDGE_PACKAGE_GUI_SMOKE=OFF. Local Release acceptance remains required.")
endif()

message(STATUS "AssetBridge package contents and permitted smoke tests passed.")
