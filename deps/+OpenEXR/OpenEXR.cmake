add_cmake_project(OpenEXR
    URL https://github.com/AcademySoftwareFoundation/openexr/archive/refs/tags/v3.1.12.zip
    URL_HASH SHA256=7ba8ff9f9dec60039caef040790c1410c2cbd880c8072640a3c1a100338248e1
    # Note: OpenEXR 2.x patch removed - cstdint includes are standard in 3.x
    CMAKE_ARGS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        # Force dynamic CRT (/MD) to match preFlight
        -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL
        -DBUILD_TESTING=OFF
        -DOPENEXR_INSTALL_EXAMPLES=OFF
        -DOPENEXR_BUILD_TOOLS=OFF
        -DBUILD_SHARED_LIBS=${BUILD_SHARED_LIBS}
)

set(DEP_OpenEXR_DEPENDS ZLIB Imath)
