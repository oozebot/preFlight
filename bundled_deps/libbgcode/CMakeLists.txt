###|/ Copyright (c) Prusa Research 2023 - libbgcode
###|/ Copyright (c) preFlight 2025+ oozeBot, LLC - Ported to preFlight
###|/
###|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
###|/
# LibBGCode - Bundled version for preFlight
# Original: https://github.com/prusa3d/libbgcode (AGPL-3.0)

cmake_minimum_required(VERSION 3.12)

# Version (from original LibBGCode pyproject.toml)
set(LIBBGCODE_VERSION "0.2.0")

# Find required packages
find_package(heatshrink REQUIRED)

# Core library
add_library(bgcode_core STATIC
    LibBGCode/core/core.cpp
    LibBGCode/core/core.hpp
    LibBGCode/core/core_impl.hpp
    LibBGCode/core/export.h
)
target_include_directories(bgcode_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(bgcode_core PRIVATE LibBGCode_VERSION="${LIBBGCODE_VERSION}")
target_link_libraries(bgcode_core PRIVATE ZLIB::ZLIB)

# Binarize library (depends on core)
add_library(bgcode_binarize STATIC
    LibBGCode/binarize/binarize.cpp
    LibBGCode/binarize/binarize.hpp
    LibBGCode/binarize/meatpack.cpp
    LibBGCode/binarize/meatpack.hpp
    LibBGCode/binarize/export.h
)
target_include_directories(bgcode_binarize PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bgcode_binarize
    PUBLIC bgcode_core
    PRIVATE heatshrink::heatshrink_dynalloc ZLIB::ZLIB
)

# Convert library (depends on binarize)
add_library(bgcode_convert STATIC
    LibBGCode/convert/convert.cpp
    LibBGCode/convert/convert.hpp
    LibBGCode/convert/export.h
)
target_include_directories(bgcode_convert PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bgcode_convert
    PUBLIC bgcode_binarize
    PRIVATE boost_headeronly
)

# Create alias targets to match the original LibBGCode::* namespace
add_library(LibBGCode::bgcode_core ALIAS bgcode_core)
add_library(LibBGCode::bgcode_binarize ALIAS bgcode_binarize)
add_library(LibBGCode::bgcode_convert ALIAS bgcode_convert)
