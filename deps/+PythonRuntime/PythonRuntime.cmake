#/|/ Copyright (c) preFlight 2025+ oozeBot, LLC
#/|/
#/|/ Released under AGPLv3 or higher
#/|/
# Downloads the Python embeddable runtime for bundling with preFlight.
# End users don't need Python installed - the runtime ships with the app.
#
# Windows: official "embeddable package" from python.org + pip bootstrap
# Linux:   built from source with --enable-shared for relocatable libpython
# macOS:   built from source with --enable-shared for relocatable libpython
#
# The runtime is installed to ${DESTDIR}/python-runtime/ and the main build
# copies it alongside the binary during the build step.
#
# IMPORTANT: The version here MUST match the Python version used to build
# preFlight (found by find_package(Python3) in the root CMakeLists.txt).
# The root CMakeLists.txt enforces this with a version check.

set(_py_version "3.14.4")
set(_py_ver_nodot "314")
set(_py_destdir "${${PROJECT_NAME}_DEP_INSTALL_PREFIX}/python-runtime")

if (MSVC)
    # Detect target architecture from the compiler
    if (CMAKE_GENERATOR_PLATFORM STREQUAL "ARM64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "ARM64")
        set(_py_arch "arm64")
        set(_py_hash "55bc0d232f16a8450e6c37c774e4cf397f5212d5e4603e5b92bd1fe85451eead")
    else ()
        set(_py_arch "amd64")
        set(_py_hash "cda80a9b1e75c0f1b4f9872ca1b417f0d19bce32facc811aea9180e70fad5fb9")
    endif ()

    set(_py_filename "python-${_py_version}-embed-${_py_arch}.zip")
    set(_py_url "https://www.python.org/ftp/python/${_py_version}/${_py_filename}")

    ExternalProject_Add(dep_PythonRuntime
        URL             "${_py_url}"
        URL_HASH        SHA256=${_py_hash}
        DOWNLOAD_DIR    ${${PROJECT_NAME}_DEP_DOWNLOAD_DIR}/PythonRuntime
        DOWNLOAD_NO_EXTRACT OFF
        SOURCE_DIR      "${_py_destdir}"
        CONFIGURE_COMMAND ""
        BUILD_COMMAND     ""
        # Enable site imports so standalone python.exe can find site-packages.
        # pip is not part of the embeddable package; the app build pre-installs it into its
        # copy of this runtime (cmake/modules/DownloadGetPip.cmake).
        INSTALL_COMMAND ${CMAKE_COMMAND}
            -DPTH_FILE=${_py_destdir}/python${_py_ver_nodot}._pth
            -P ${CMAKE_CURRENT_LIST_DIR}/enable_site_imports.cmake
    )

elseif (APPLE)
    # macOS: build Python from source with --enable-shared for embedding.
    # ensurepip included so pip is available out of the box.
    set(_py_hash "d923c51303e38e249136fc1bdf3568d56ecb03214efdef48516176d3d7faaef8")
    set(_py_url "https://www.python.org/ftp/python/${_py_version}/Python-${_py_version}.tar.xz")

    include(ProcessorCount)
    ProcessorCount(_nproc)
    if (_nproc EQUAL 0)
        set(_nproc 4)
    endif ()

    ExternalProject_Add(dep_PythonRuntime
        URL             "${_py_url}"
        URL_HASH        SHA256=${_py_hash}
        DOWNLOAD_DIR    ${${PROJECT_NAME}_DEP_DOWNLOAD_DIR}/PythonRuntime
        BUILD_IN_SOURCE ON
        CONFIGURE_COMMAND ./configure
            --prefix=${_py_destdir}
            --enable-shared
            --disable-test-modules
            "CFLAGS=-mmacosx-version-min=${DEP_OSX_TARGET}"
            "LDFLAGS=-mmacosx-version-min=${DEP_OSX_TARGET} -Wl,-rpath,@loader_path"
        BUILD_COMMAND make -j${_nproc}
        INSTALL_COMMAND make install
    )

else ()
    # Linux: build Python from source with --enable-shared for embedding.
    # RPATH is set to $ORIGIN so libpython can be found relative to the binary.
    # ensurepip included so pip is available out of the box.
    set(_py_hash "d923c51303e38e249136fc1bdf3568d56ecb03214efdef48516176d3d7faaef8")
    set(_py_url "https://www.python.org/ftp/python/${_py_version}/Python-${_py_version}.tar.xz")

    include(ProcessorCount)
    ProcessorCount(_nproc)
    if (_nproc EQUAL 0)
        set(_nproc 4)
    endif ()

    ExternalProject_Add(dep_PythonRuntime
        URL             "${_py_url}"
        URL_HASH        SHA256=${_py_hash}
        DOWNLOAD_DIR    ${${PROJECT_NAME}_DEP_DOWNLOAD_DIR}/PythonRuntime
        BUILD_IN_SOURCE ON
        CONFIGURE_COMMAND ./configure
            --prefix=${_py_destdir}
            --enable-shared
            --disable-test-modules
            "LDFLAGS=-Wl,-rpath,\\$$ORIGIN"
        BUILD_COMMAND make -j${_nproc}
        INSTALL_COMMAND make install
    )
endif ()
