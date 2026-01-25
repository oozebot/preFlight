cmake_minimum_required(VERSION 3.13)
project(OCCTWrapper)

if (APPLE)
    # TODO: we need to fix notarization with the separate shared library
    add_library(OCCTWrapper STATIC OCCTWrapper.cpp)
else ()
    add_library(OCCTWrapper MODULE OCCTWrapper.cpp)
endif ()

# Match MSBuild output path for single-config generators
get_property(_is_multi GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (_is_multi)
    set(_occt_output_dir "${CMAKE_BINARY_DIR}/src")
else ()
    set(_occt_output_dir "${CMAKE_BINARY_DIR}/src/${CMAKE_BUILD_TYPE}")
endif ()

set_target_properties(OCCTWrapper
    PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${_occt_output_dir}"
    RUNTIME_OUTPUT_DIRECTORY "${_occt_output_dir}"
    PREFIX ""
)

include(GenerateExportHeader)

generate_export_header(OCCTWrapper)

# OCCT 7.8.0 reorganized DataExchange toolkits:
# - TKSTEP, TKSTEP209, TKSTEPAttr, TKSTEPBase merged into TKDESTEP
# - TKXDESTEP merged into TKDESTEP (XCAF STEP support included)
# - TKXSBase still exists (contains XSControl_Reader, Interface_* classes)
# - TKDE is new Data Exchange base layer
find_package(OpenCASCADE 7.9.3 REQUIRED)

set(OCCT_LIBS
    TKDESTEP
    TKXCAF
    TKXSBase
    TKDE
    TKVCAF
    TKCAF
    TKLCAF
    TKCDF
    TKV3d
    TKService
    TKMesh
    TKBO
    TKPrim
    TKHLR
    TKShHealing
    TKTopAlgo
    TKGeomAlgo
    TKBRep
    TKGeomBase
    TKG3d
    TKG2d
    TKMath
    TKernel
)

slic3r_remap_configs("${OCCT_LIBS}" RelWithDebInfo Release)

target_include_directories(OCCTWrapper PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(OCCTWrapper PUBLIC ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(OCCTWrapper ${OCCT_LIBS})
target_link_libraries(OCCTWrapper libslic3r admesh)

include(GNUInstallDirs)

install(TARGETS OCCTWrapper DESTINATION "${CMAKE_INSTALL_BINDIR}")

