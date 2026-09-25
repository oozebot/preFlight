# Shared tbbmalloc + tbbmalloc_proxy for the optional process-allocator replacement
# (PREFLIGHT_TBBMALLOC_PROXY). Built from the same oneTBB source as the static TBB package,
# but malloc-only and shared, and installed into its own prefix subdirectory: a same-prefix
# install would overwrite the static package's lib/tbbmalloc.lib and lib/cmake/TBB configs.
# The proxy patches the release ucrtbase heap only; it is inert in /MDd Debug builds.
# oneTBB skips the proxy on Windows ARM64, so the package is not built there.
#
# The patch makes the proxy dry-run every replacement a runtime module is about to receive
# (export present, code page writable, trampoline location reachable and executable) and skip
# the module when any step is refused, instead of aborting the process from DllMain. The
# process then keeps the CRT allocator; an unknown prologue already took that path upstream.
# The patch carries plain ---/+++ headers on purpose: git apply treats a "diff --git" patch as
# relative to the enclosing checkout root and silently skips it when run from a deps source dir.
if (WIN32 AND NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "ARM64")
add_cmake_project(
    TBBMallocProxy
    URL "https://github.com/oneapi-src/oneTBB/archive/refs/tags/v2022.3.0.zip"
    URL_HASH SHA256=4f47379064f99cc50da8dde85e27651d3609ac6c3e0941b1c728a1b2dd1e4b68
    PATCH_COMMAND ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/0001-skip-replacement-when-a-precheck-fails.patch
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX:STRING=${${PROJECT_NAME}_DEP_INSTALL_PREFIX}/tbbmalloc-proxy
        -DBUILD_SHARED_LIBS:BOOL=ON
        -DTBB_BUILD=OFF
        -DTBBMALLOC_BUILD=ON
        -DTBBMALLOC_PROXY_BUILD=ON
        -DTBB_TEST=OFF
        -DTBB_STRICT=OFF
        -DCMAKE_DEBUG_POSTFIX=_debug
)
endif ()
