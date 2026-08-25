include(CMakeParseArguments)

function(prepare_borealis output_var)
    set(one_value_args SOURCE PATCH)
    cmake_parse_arguments(PREPARE_BOREALIS "" "${one_value_args}" "" ${ARGN})

    if(NOT PREPARE_BOREALIS_SOURCE OR NOT IS_DIRECTORY "${PREPARE_BOREALIS_SOURCE}/library")
        message(FATAL_ERROR "Invalid Borealis source directory: ${PREPARE_BOREALIS_SOURCE}")
    endif()

    if(NOT PREPARE_BOREALIS_PATCH OR NOT EXISTS "${PREPARE_BOREALIS_PATCH}")
        message(FATAL_ERROR "Borealis patch not found: ${PREPARE_BOREALIS_PATCH}")
    endif()

    find_package(Git REQUIRED)

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${PREPARE_BOREALIS_SOURCE}" rev-parse HEAD
        RESULT_VARIABLE git_result
        OUTPUT_VARIABLE borealis_revision
        ERROR_VARIABLE git_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT git_result EQUAL 0)
        message(FATAL_ERROR "Unable to read the Borealis revision: ${git_error}")
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${PREPARE_BOREALIS_SOURCE}" status --porcelain
        RESULT_VARIABLE git_status_result
        OUTPUT_VARIABLE git_status
        ERROR_VARIABLE git_status_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT git_status_result EQUAL 0)
        message(FATAL_ERROR "Unable to inspect the Borealis source: ${git_status_error}")
    endif()
    if(NOT "${git_status}" STREQUAL "")
        message(FATAL_ERROR "Borealis must be clean before configuring the project")
    endif()

    file(SHA256 "${PREPARE_BOREALIS_PATCH}" patch_hash)
    set(patch_state "1:${borealis_revision}:${patch_hash}")
    set(staged_source "${CMAKE_BINARY_DIR}/_deps/borealis-src")
    set(stamp_file "${staged_source}/.moonlight-patch-state")

    set(current_state "")
    if(EXISTS "${stamp_file}")
        file(READ "${stamp_file}" current_state)
    endif()

    set(prepare_required TRUE)
    if("${current_state}" STREQUAL "${patch_state}" AND IS_DIRECTORY "${staged_source}/.git")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply --reverse --check --whitespace=nowarn "${PREPARE_BOREALIS_PATCH}"
            WORKING_DIRECTORY "${staged_source}"
            RESULT_VARIABLE staged_patch_result
            ERROR_QUIET
        )
        if(staged_patch_result EQUAL 0)
            set(prepare_required FALSE)
        endif()
    endif()

    if(prepare_required)
        message(STATUS "Preparing patched Borealis source")
        file(REMOVE_RECURSE "${staged_source}")
        file(MAKE_DIRECTORY "${staged_source}")
        file(COPY "${PREPARE_BOREALIS_SOURCE}/library" DESTINATION "${staged_source}")

        execute_process(
            COMMAND "${GIT_EXECUTABLE}" init --quiet
            WORKING_DIRECTORY "${staged_source}"
            RESULT_VARIABLE git_init_result
            ERROR_VARIABLE git_init_error
        )
        if(NOT git_init_result EQUAL 0)
            message(FATAL_ERROR "Unable to initialize the staged Borealis source: ${git_init_error}")
        endif()

        execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply --check --whitespace=nowarn "${PREPARE_BOREALIS_PATCH}"
            WORKING_DIRECTORY "${staged_source}"
            RESULT_VARIABLE patch_check_result
            ERROR_VARIABLE patch_check_error
        )
        if(NOT patch_check_result EQUAL 0)
            message(FATAL_ERROR "Borealis patch does not apply to ${borealis_revision}: ${patch_check_error}")
        endif()

        execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply --whitespace=nowarn "${PREPARE_BOREALIS_PATCH}"
            WORKING_DIRECTORY "${staged_source}"
            RESULT_VARIABLE patch_result
            ERROR_VARIABLE patch_error
        )
        if(NOT patch_result EQUAL 0)
            message(FATAL_ERROR "Unable to apply the Borealis patch: ${patch_error}")
        endif()

        file(WRITE "${stamp_file}" "${patch_state}")
    endif()

    set(${output_var} "${staged_source}" PARENT_SCOPE)
endfunction()
