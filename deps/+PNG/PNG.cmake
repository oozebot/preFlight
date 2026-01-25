# - Removed CMakeLists.txt.patched: upstream CMakeLists works directly
# - Removed PNG.patch: macOS fp.h fix no longer needed (code modernized)
# - 6 years of bug fixes and security improvements
if (MSVC OR APPLE)
    if (APPLE)
        # Only disable NEON extension for Apple ARM builds, leave it enabled for Raspberry PI.
        set(_disable_neon_extension "-DPNG_ARM_NEON:STRING=off")
    else ()
        set(_disable_neon_extension "")
    endif ()

    add_cmake_project(PNG
        URL https://github.com/glennrp/libpng/archive/refs/tags/v1.6.44.zip
        URL_HASH SHA256=0b35b4bbdf454d589bf8195bc281fefecc4b2529b42ddfbe8b6c108b986841f9
        CMAKE_ARGS
            -DPNG_SHARED=OFF
            -DPNG_STATIC=ON
            -DPNG_PREFIX=prusaslicer_
            -DPNG_TESTS=OFF
            -DPNG_EXECUTABLES=OFF
            ${_disable_neon_extension}
    )

    set(DEP_PNG_DEPENDS ZLIB)
endif()
