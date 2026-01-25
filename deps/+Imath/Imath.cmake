add_cmake_project(Imath
    URL https://github.com/AcademySoftwareFoundation/Imath/archive/refs/tags/v3.1.12.zip
    URL_HASH SHA256=82d8f31c46e73dba92525bea29c4fe077f6a7d3b978d5067a15030413710bf46
    CMAKE_ARGS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        # Force dynamic CRT (/MD) to match preFlight - upstream defaults to static (/MT)
        -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL
        -DBUILD_TESTING=OFF
        -DBUILD_SHARED_LIBS=${BUILD_SHARED_LIBS}
)
