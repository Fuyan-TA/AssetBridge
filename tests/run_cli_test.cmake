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
if(DEFINED ASSETBRIDGE_TARGET)
    if(ASSETBRIDGE_COMMAND STREQUAL "convert")
        list(APPEND assetbridge_arguments "--to" "${ASSETBRIDGE_TARGET}")
    else()
        list(APPEND assetbridge_arguments "--target" "${ASSETBRIDGE_TARGET}")
    endif()
endif()
if(DEFINED ASSETBRIDGE_OUTPUT)
    list(APPEND assetbridge_arguments "--output" "${ASSETBRIDGE_OUTPUT}")
endif()
if(DEFINED ASSETBRIDGE_OPTION)
    list(APPEND assetbridge_arguments "${ASSETBRIDGE_OPTION}")
endif()

if(ASSETBRIDGE_CLEAN_OUTPUT AND DEFINED ASSETBRIDGE_OUTPUT)
    file(REMOVE_RECURSE "${ASSETBRIDGE_OUTPUT}")
endif()

execute_process(
    COMMAND "${ASSETBRIDGE_CLI}" ${assetbridge_arguments}
    RESULT_VARIABLE actual_exit
    OUTPUT_VARIABLE actual_stdout
    ERROR_VARIABLE actual_stderr
    ENCODING UTF-8
)

message(STATUS "CLI exit code: ${actual_exit}")
if(NOT actual_stdout STREQUAL "" AND NOT DEFINED ASSETBRIDGE_VALIDATE_JSON)
    message(STATUS "CLI stdout:\n${actual_stdout}")
endif()
if(NOT actual_stderr STREQUAL "")
    message(STATUS "CLI stderr:\n${actual_stderr}")
endif()

if(NOT "${actual_exit}" STREQUAL "${ASSETBRIDGE_EXPECTED_EXIT}")
    message(FATAL_ERROR
        "Expected exit code ${ASSETBRIDGE_EXPECTED_EXIT}, got ${actual_exit}")
endif()

if(DEFINED ASSETBRIDGE_OUTPUT AND EXISTS "${ASSETBRIDGE_OUTPUT}")
    file(GLOB temporary_directories "${ASSETBRIDGE_OUTPUT}/.assetbridge-tmp-*")
    if(temporary_directories)
        message(FATAL_ERROR "Conversion left a temporary directory: ${temporary_directories}")
    endif()
    if(ASSETBRIDGE_EXPECT_EMPTY_OUTPUT)
        file(GLOB output_children "${ASSETBRIDGE_OUTPUT}/*")
        if(output_children)
            message(FATAL_ERROR "Failed conversion left output content: ${output_children}")
        endif()
    endif()
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

function(require_no_fragments output encoded_fragments)
    if(encoded_fragments STREQUAL "")
        return()
    endif()

    string(REPLACE "|" ";" unexpected_fragments "${encoded_fragments}")
    foreach(fragment IN LISTS unexpected_fragments)
        string(FIND "${output}" "${fragment}" fragment_position)
        if(NOT fragment_position EQUAL -1)
            message(FATAL_ERROR "Found unexpected output fragment: ${fragment}")
        endif()
    endforeach()
endfunction()

require_all_fragments("${actual_stdout}" "${ASSETBRIDGE_EXPECTED_STDOUT}")
require_all_fragments("${actual_stderr}" "${ASSETBRIDGE_EXPECTED_STDERR}")
require_any_fragment("${actual_stderr}" "${ASSETBRIDGE_EXPECTED_STDERR_ANY}")
require_no_fragments("${actual_stdout}" "${ASSETBRIDGE_UNEXPECTED_STDOUT}")
require_no_fragments("${actual_stderr}" "${ASSETBRIDGE_UNEXPECTED_STDERR}")

if(DEFINED ASSETBRIDGE_VALIDATE_JSON)
    if(NOT DEFINED ASSETBRIDGE_JSON_VALIDATOR OR NOT DEFINED ASSETBRIDGE_JSON_OUTPUT)
        message(FATAL_ERROR "JSON validation requires a validator and output path")
    endif()
    if(NOT actual_stderr STREQUAL "")
        message(FATAL_ERROR "JSON mode wrote unexpected stderr: ${actual_stderr}")
    endif()

    file(WRITE "${ASSETBRIDGE_JSON_OUTPUT}" "${actual_stdout}")
    set(validator_arguments
        "${ASSETBRIDGE_VALIDATE_JSON}"
        "${ASSETBRIDGE_JSON_OUTPUT}"
    )
    if(DEFINED ASSETBRIDGE_INPUT)
        list(APPEND validator_arguments "${ASSETBRIDGE_INPUT}")
    endif()

    if(ASSETBRIDGE_COMPARE_TEXT)
        set(text_arguments)
        if(DEFINED ASSETBRIDGE_COMMAND)
            list(APPEND text_arguments "${ASSETBRIDGE_COMMAND}")
        endif()
        if(DEFINED ASSETBRIDGE_INPUT)
            list(APPEND text_arguments "${ASSETBRIDGE_INPUT}")
        endif()
        if(DEFINED ASSETBRIDGE_TARGET)
            list(APPEND text_arguments "--target" "${ASSETBRIDGE_TARGET}")
        endif()

        execute_process(
            COMMAND "${ASSETBRIDGE_CLI}" ${text_arguments}
            RESULT_VARIABLE text_exit
            OUTPUT_VARIABLE text_stdout
            ERROR_VARIABLE text_stderr
            ENCODING UTF-8
        )
        if(NOT "${text_exit}" STREQUAL "${ASSETBRIDGE_EXPECTED_EXIT}")
            message(FATAL_ERROR
                "Text comparison command exited ${text_exit}, expected ${ASSETBRIDGE_EXPECTED_EXIT}")
        endif()
        set(text_output "${text_stdout}${text_stderr}")
        set(text_output_path "${ASSETBRIDGE_JSON_OUTPUT}.txt")
        file(WRITE "${text_output_path}" "${text_output}")
        list(APPEND validator_arguments "${text_output_path}")
    endif()

    execute_process(
        COMMAND "${ASSETBRIDGE_JSON_VALIDATOR}" ${validator_arguments}
        RESULT_VARIABLE validator_exit
        OUTPUT_VARIABLE validator_stdout
        ERROR_VARIABLE validator_stderr
        ENCODING UTF-8
    )
    if(NOT validator_stdout STREQUAL "")
        message(STATUS "${validator_stdout}")
    endif()
    if(NOT "${validator_exit}" STREQUAL "0")
        message(FATAL_ERROR
            "JSON validator failed with exit ${validator_exit}: ${validator_stderr}")
    endif()
endif()
