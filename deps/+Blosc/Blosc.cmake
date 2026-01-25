if(BUILD_SHARED_LIBS)
    set(_build_shared ON)
    set(_build_static OFF)
else()
    set(_build_shared OFF)
    set(_build_static ON)
endif()

# Platform-specific arguments
if(APPLE)
    # SSE2 support is dropped in Clang of newer Xcode (16.3+) versions
    list(APPEND _blosc_cmake_args -DDEACTIVATE_SSE2=ON)
endif()

add_cmake_project(Blosc
    URL https://github.com/Blosc/c-blosc/archive/refs/tags/v1.21.6.zip
    URL_HASH SHA256=1919c97d55023c04aa8771ea8235b63e9da3c22e3d2a68340b33710d19c2a2eb
    CMAKE_ARGS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        # Force dynamic CRT (/MD) to match preFlight
        -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL
        -DBUILD_SHARED=${_build_shared}
        -DBUILD_STATIC=${_build_static}
        -DBUILD_TESTS=OFF
        -DBUILD_BENCHMARKS=OFF
        -DPREFER_EXTERNAL_ZLIB=ON
        ${_blosc_cmake_args}
)

set(DEP_Blosc_DEPENDS ZLIB)
