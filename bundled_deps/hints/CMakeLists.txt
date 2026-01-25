cmake_minimum_required(VERSION 3.13)
project(HintsToPot)

add_executable(hintsToPot
    HintsToPot.cpp)

target_link_libraries(hintsToPot PRIVATE boost_libs)
set_target_properties(hintsToPot PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)

#encoding_check(HintsToPot)



