if(NOT DEFINED ASSETBRIDGE_CLI)
    message(FATAL_ERROR "ASSETBRIDGE_CLI is required")
endif()

if(NOT DEFINED ASSETBRIDGE_EXPECTED_EXIT)
    message(FATAL_ERROR "ASSETBRIDGE_EXPECTED_EXIT is required")
endif()

set(assetbridge_arguments)
if(DEFINED ASSETBRIDGE_COMMAND)
    list(APPEND assetbridge_arguments "${ASSETBRIDGE_COMMAND}")
endif()
if(DEFINED ASSETBRIDGE_INPUT)
    list(APPEND assetbridge_arguments "${ASSETBRIDGE_INPUT}")
endif()

execute_process(
    COMMAND "${ASSETBRIDGE_CLI}" ${assetbridge_arguments}
    RESULT_VARIABLE actual_exit
    OUTPUT_VARIABLE actual_stdout
    ERROR_VARIABLE actual_stderr
    ENCODING UTF-8
)

message(STATUS "CLI exit code: ${actual_exit}")
if(NOT actual_stdout STREQUAL "")
    message(STATUS "CLI stdout:\n${actual_stdout}")
endif()
if(NOT actual_stderr STREQUAL "")
    message(STATUS "CLI stderr:\n${actual_stderr}")
endif()

if(NOT "${actual_exit}" STREQUAL "${ASSETBRIDGE_EXPECTED_EXIT}")
    message(FATAL_ERROR
        "Expected exit code ${ASSETBRIDGE_EXPECTED_EXIT}, got ${actual_exit}")
endif()

function(require_all_fragments output encoded_fragments)
    if(encoded_fragments STREQUAL "")
        return()
    endif()

    string(REPLACE "|" ";" expected_fragments "${encoded_fragments}")
    foreach(fragment IN LISTS expected_fragments)
        string(FIND "${output}" "${fragment}" fragment_position)
        if(fragment_position EQUAL -1)
            message(FATAL_ERROR "Missing expected output fragment: ${fragment}")
        endif()
    endforeach()
endfunction()

function(require_any_fragment output encoded_fragments)
    if(encoded_fragments STREQUAL "")
        return()
    endif()

    string(REPLACE "|" ";" expected_fragments "${encoded_fragments}")
    set(any_fragment_found FALSE)
    foreach(fragment IN LISTS expected_fragments)
        string(FIND "${output}" "${fragment}" fragment_position)
        if(NOT fragment_position EQUAL -1)
            set(any_fragment_found TRUE)
        endif()
    endforeach()
    if(NOT any_fragment_found)
        message(FATAL_ERROR "None of the alternative output fragments were found")
    endif()
endfunction()

require_all_fragments("${actual_stdout}" "${ASSETBRIDGE_EXPECTED_STDOUT}")
require_all_fragments("${actual_stderr}" "${ASSETBRIDGE_EXPECTED_STDERR}")
require_any_fragment("${actual_stderr}" "${ASSETBRIDGE_EXPECTED_STDERR_ANY}")
