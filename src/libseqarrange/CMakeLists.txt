find_package(Z3 REQUIRED)
slic3r_remap_configs("z3::libz3" RelWithDebInfo Release)



add_library(libseqarrange STATIC src/seq_interface.cpp src/seq_preprocess.cpp src/seq_sequential.cpp src/seq_utilities.cpp)
target_include_directories(libseqarrange PUBLIC include PRIVATE src )
target_link_libraries(libseqarrange PUBLIC libslic3r PRIVATE z3::libz3)
# Uses Clipper2 indirectly through libslic3r's PUBLIC link

add_executable(sequential_decimator src/sequential_decimator.cpp)
target_include_directories(sequential_decimator PRIVATE include)
target_link_libraries(sequential_decimator PRIVATE libseqarrange)
set_target_properties(sequential_decimator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)