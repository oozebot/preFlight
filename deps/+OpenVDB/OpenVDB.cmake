if(BUILD_SHARED_LIBS)
    set(_build_shared ON)
    set(_build_static OFF)
else()
    set(_build_shared OFF)
    set(_build_static ON)
endif()

# vdb_print is just a diagnostic utility, not needed for preFlight
set (_openvdb_vdbprint OFF)

add_cmake_project(OpenVDB
    URL https://github.com/AcademySoftwareFoundation/openvdb/archive/refs/tags/v11.0.0.zip
    URL_HASH SHA256=db7e1aacd0a634195574b2e6a43d268d063830628501fd0a94a99bf252d01fb6

    CMAKE_ARGS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        # Force dynamic CRT (/MD) to match preFlight - upstream defaults to static (/MT)
        -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL
        -DOPENVDB_BUILD_PYTHON_MODULE=OFF
        -DUSE_BLOSC=ON
        -DOPENVDB_CORE_SHARED=${_build_shared}
        -DOPENVDB_CORE_STATIC=${_build_static}
        -DOPENVDB_ENABLE_RPATH:BOOL=OFF
        -DTBB_STATIC=${_build_static}
        -DOPENVDB_BUILD_VDB_PRINT=${_openvdb_vdbprint}
        -DOPENVDB_BUILD_NANOVDB=OFF
        -DOPENVDB_BUILD_UNITTESTS=OFF
)

set(DEP_OpenVDB_DEPENDS TBB Blosc OpenEXR Boost)
