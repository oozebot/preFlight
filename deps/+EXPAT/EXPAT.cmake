# The integer overflow fix (CVE-2022-22826) was already in 2.4.3 upstream.
# v2.6.4 includes additional security fixes: CVE-2022-40674, CVE-2024-28757, etc.
add_cmake_project(EXPAT
  URL https://github.com/libexpat/libexpat/archive/refs/tags/R_2_6_4.zip
  URL_HASH SHA256=80e29f3af7372def1e36f4bff26987e21cdcb6dd65c4a34d52974c6eb60f91ad
  SOURCE_SUBDIR expat
  CMAKE_ARGS
    -DEXPAT_BUILD_TOOLS:BOOL=OFF
    -DEXPAT_BUILD_EXAMPLES:BOOL=OFF
    -DEXPAT_BUILD_TESTS:BOOL=OFF
    -DEXPAT_BUILD_DOCS=OFF
    -DEXPAT_BUILD_PKGCONFIG=OFF
)
