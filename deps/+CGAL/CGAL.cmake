add_cmake_project(
    CGAL
    URL      https://github.com/CGAL/cgal/archive/refs/tags/v6.1.zip
    URL_HASH SHA256=ac8f61bef8a7d8732d041d0953db3203c93427b1346389c57fa4c567e36672d4
    CMAKE_ARGS
        -DCGAL_CMAKE_EXACT_NT_BACKEND=BOOST_BACKEND
)

include(GNUInstallDirs)

# GMP and MPFR no longer needed - CGAL 6.0+ uses Boost.Multiprecision
set(DEP_CGAL_DEPENDS Boost)
