#/|/ Copyright (c) preFlight 2025+ oozeBot, LLC
#/|/
#/|/ Released under AGPLv3 or higher
#/|/
# Downloads get-pip.py to the bundled Python directory if not already present.
# Arguments: -DPYTHON_DIR=<path_to_python_dir>
set(_getpip "${PYTHON_DIR}/get-pip.py")
if (NOT EXISTS "${_getpip}")
    message(STATUS "Downloading get-pip.py to ${PYTHON_DIR}...")
    # URL is a rolling latest - no hash pin (would break on every pypa release).
    # HTTPS provides transport integrity. This runs at build time, not end-user.
    file(DOWNLOAD
        "https://bootstrap.pypa.io/get-pip.py"
        "${_getpip}"
        STATUS _dl_status
        TIMEOUT 30
    )
    list(GET _dl_status 0 _dl_code)
    if (NOT _dl_code EQUAL 0)
        message(STATUS "get-pip.py download failed (status: ${_dl_status}). "
                       "Users can download it manually from https://bootstrap.pypa.io/get-pip.py")
    else ()
        message(STATUS "get-pip.py downloaded successfully")
    endif ()
endif ()

# Pre-install pip into the bundled runtime so end users never bootstrap it (Windows;
# on mac/Linux pip is installed by the runtime's make install). Best-effort: a failure
# here (e.g. no build-time network) must not fail the build.
if (EXISTS "${_getpip}")
    set(_pyexe "${PYTHON_DIR}/python.exe")
    if (EXISTS "${_pyexe}" AND NOT EXISTS "${PYTHON_DIR}/Lib/site-packages/pip")
        message(STATUS "Installing pip into the bundled runtime...")
        execute_process(
            COMMAND "${_pyexe}" "${_getpip}" --no-warn-script-location
            RESULT_VARIABLE _pip_result
            ERROR_VARIABLE _pip_err
        )
        if (NOT _pip_result EQUAL 0)
            message(STATUS "pip pre-install failed (${_pip_result}); bootstrap later with "
                           "'python python\\get-pip.py'. ${_pip_err}")
        else ()
            message(STATUS "pip pre-installed into the bundled runtime")
        endif ()
    endif ()
endif ()

# Precompile the loose runtime (Lib + site-packages incl. pip) to hash-pinned bytecode and forbid
# runtime bytecode writes, so a direct python.exe invocation neither recompiles every run nor
# pollutes the install dir. The stdlib ships read-only in pythonXX.zip and needs nothing. (Windows;
# on mac/Linux the pack scripts do this.)
set(_pyexe "${PYTHON_DIR}/python.exe")
if (EXISTS "${_pyexe}")
    execute_process(
        COMMAND "${_pyexe}" -m compileall -q --invalidation-mode unchecked-hash "${PYTHON_DIR}/Lib"
        RESULT_VARIABLE _cc_result
    )
    # Belt for direct python.exe use: a regular site .pth whose line starts with 'import' is executed
    # at startup (site is enabled in the embeddable's ._pth - that is how pip is found). The ._pth
    # itself only accepts 'import site', so the guard must live in a site-packages .pth, not the ._pth.
    if (EXISTS "${PYTHON_DIR}/Lib/site-packages")
        file(WRITE "${PYTHON_DIR}/Lib/site-packages/zzz_preflight_no_bytecode.pth"
             "import sys; sys.dont_write_bytecode = True\n")
    endif ()
    if (_cc_result EQUAL 0)
        message(STATUS "Precompiled bundled Python (hash-pinned) and added no-bytecode guard")
    else ()
        message(STATUS "stdlib precompile skipped/failed; runtime may write bytecode into the install")
    endif ()
endif ()

# A build with the preprocessor feature enabled must ship a working pip; never silently produce a
# console that cannot install packages (e.g. a build with no access to bootstrap.pypa.io).
if (NOT EXISTS "${PYTHON_DIR}/Lib/site-packages/pip")
    message(FATAL_ERROR
        "pip was not installed into the bundled runtime (${PYTHON_DIR}). The Python Console cannot "
        "install packages without it. Ensure build-time network access to bootstrap.pypa.io, or "
        "bootstrap pip manually before packaging.")
endif ()
