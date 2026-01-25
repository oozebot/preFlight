###|/ Copyright (c) preFlight 2025+ oozeBot, LLC - Integrated into preFlight
###|/ Copyright (c) Angus Johnson 2010-2025
###|/
###|/ Clipper2 is released under the Boost Software License
###|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
###|/

cmake_minimum_required(VERSION 3.13)

project(Clipper2)

# Clipper2 library sources
set(CLIPPER2_SOURCES
    src/clipper.engine.cpp
    src/clipper.offset.cpp
    src/clipper.rectclip.cpp
    src/clipper.triangulation.cpp
)

# Clipper2 library headers
set(CLIPPER2_HEADERS
    include/clipper2/clipper.h
    include/clipper2/clipper.core.h
    include/clipper2/clipper.engine.h
    include/clipper2/clipper.export.h
    include/clipper2/clipper.minkowski.h
    include/clipper2/clipper.offset.h
    include/clipper2/clipper.rectclip.h
    include/clipper2/clipper.triangulation.h
    include/clipper2/clipper.version.h
)

# Create static library
add_library(clipper2 STATIC ${CLIPPER2_SOURCES} ${CLIPPER2_HEADERS})

# Set include directories
target_include_directories(clipper2 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Set C++17 standard (Clipper2 requires C++17)
target_compile_features(clipper2 PUBLIC cxx_std_17)

# Enable high-precision mode for Clipper2
# Uses origin-centered line intersection algorithm to reduce floating-point
# precision loss with large coordinates (~10-20% performance cost for correctness)
# CLIPPER2_MAX_DECIMAL_PRECISION increased from default 8 to 10
target_compile_definitions(clipper2 PUBLIC
    CLIPPER2_HI_PRECISION=1
    CLIPPER2_MAX_DECIMAL_PRECISION=10
)

# Enable Z coordinate support for ClipperZUtils
# USINGZ adds Z member to Point64/PointD and enables SetZCallback
target_compile_definitions(clipper2 PUBLIC USINGZ)

# Platform-specific settings
if(MSVC)
    target_compile_options(clipper2 PRIVATE
        /W3
        /wd4244  # Disable warning about conversion from double to int
        /wd4267  # Disable warning about size_t conversion
    )
else()
    target_compile_options(clipper2 PRIVATE
        -Wall
        -Wno-conversion
    )
endif()

# Install headers (optional, for future use)
install(FILES ${CLIPPER2_HEADERS} DESTINATION include/clipper2)
install(TARGETS clipper2 DESTINATION lib)
