#/|/ Copyright (c) preFlight 2025+ oozeBot, LLC
#/|/
#/|/ Released under AGPLv3 or higher
#/|/

# Applies every *.patch in PATCH_DIR to the working directory in name order and skips a patch
# that is already applied, so a source tree patched by an earlier deps build takes a newly added
# patch instead of failing on the first one.
#
# Run from the package source dir (ExternalProject runs PATCH_COMMAND there):
#   cmake -DGIT_EXECUTABLE=<git> -DPATCH_DIR=<dir> -P apply_patches.cmake

if (NOT GIT_EXECUTABLE)
    message(FATAL_ERROR "apply_patches.cmake: GIT_EXECUTABLE is not set")
endif ()
if (NOT PATCH_DIR)
    message(FATAL_ERROR "apply_patches.cmake: PATCH_DIR is not set")
endif ()

file(GLOB _patches LIST_DIRECTORIES false "${PATCH_DIR}/*.patch")
list(SORT _patches)
if (NOT _patches)
    message(FATAL_ERROR "apply_patches.cmake: no *.patch file in ${PATCH_DIR}")
endif ()

foreach (_patch IN LISTS _patches)
    get_filename_component(_name "${_patch}" NAME)

    # git apply skips a file whose path it resolves outside the working directory and still
    # exits 0, which the reverse check below would report as already applied. An empty file
    # list means nothing in the patch would be touched here.
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --numstat "${_patch}"
        RESULT_VARIABLE _rc
        OUTPUT_VARIABLE _files
        ERROR_VARIABLE _err
    )
    string(STRIP "${_files}" _files)
    if (NOT _rc EQUAL 0 OR _files STREQUAL "")
        message(FATAL_ERROR "apply_patches.cmake: ${_name} touches no file under ${CMAKE_CURRENT_SOURCE_DIR} "
                            "(git apply --numstat exit ${_rc}). A \"diff --git\" header resolves paths "
                            "against the enclosing checkout; use plain ---/+++ headers.\n${_err}")
    endif ()

    # The reverse of an applied patch applies cleanly.
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --reverse --check --ignore-space-change "${_patch}"
        RESULT_VARIABLE _rc
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if (_rc EQUAL 0)
        message(STATUS "${_name}: already applied, skipped")
        continue()
    endif ()

    message(STATUS "${_name}: applying")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --verbose --ignore-space-change --whitespace=fix "${_patch}"
        RESULT_VARIABLE _rc
    )
    if (NOT _rc EQUAL 0)
        message(FATAL_ERROR "apply_patches.cmake: ${_name} does not apply (git apply exit ${_rc})")
    endif ()
endforeach ()
