add_cmake_project(
    TBB
    URL "https://github.com/oneapi-src/oneTBB/archive/refs/tags/v2022.3.0.zip"
    URL_HASH SHA256=4f47379064f99cc50da8dde85e27651d3609ac6c3e0941b1c728a1b2dd1e4b68
    CMAKE_ARGS          
        -DTBB_BUILD_SHARED=${BUILD_SHARED_LIBS}
        -DTBB_TEST=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DCMAKE_DEBUG_POSTFIX=_debug
)


