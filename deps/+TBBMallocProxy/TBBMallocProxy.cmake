# Shared tbbmalloc + tbbmalloc_proxy for the optional process-allocator replacement
# (PREFLIGHT_TBBMALLOC_PROXY). Built from the same oneTBB source as the static TBB package,
# but malloc-only and shared, and installed into its own prefix subdirectory: a same-prefix
# install would overwrite the static package's lib/tbbmalloc.lib and lib/cmake/TBB configs.
# The proxy patches the release ucrtbase heap only; it is inert in /MDd Debug builds.
# oneTBB skips the proxy on Windows ARM64, so the package is not built there.
#
# Patch 0001 makes the proxy dry-run every replacement a runtime module is about to receive
# (export present, code page writable, trampoline location reachable and executable) and skip
# the module when any step is refused, instead of aborting the process from DllMain. The
# process then keeps the CRT allocator; an unknown prologue already took that path upstream.
# Patch 0002 adds the TBB_malloc_proxy_engage export, which runs the same replacement on
# request and returns whether ucrtbase took it. Upstream's DllMain replaces only on a static
# load. The executables load the proxy with LoadLibrary instead, so that a DLL that cannot be
# loaded leaves them on the CRT allocator rather than stopping them, and then call the export.
# The patches go through apply_patches.cmake, which applies them in name order and skips one
# that is already applied: when the patch step re-runs on a source tree from an earlier deps
# build (ExternalProject re-runs it when this command changes), only the new patches apply.
# The patches carry plain ---/+++ headers on purpose: git apply treats a "diff --git" patch as
# relative to the enclosing checkout root and silently skips it when run from a deps source dir.
if (WIN32 AND NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "ARM64")
add_cmake_project(
    TBBMallocProxy
    URL "https://github.com/oneapi-src/oneTBB/archive/refs/tags/v2022.3.0.zip"
    URL_HASH SHA256=4f47379064f99cc50da8dde85e27651d3609ac6c3e0941b1c728a1b2dd1e4b68
    PATCH_COMMAND ${CMAKE_COMMAND} "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}" "-DPATCH_DIR=${CMAKE_CURRENT_LIST_DIR}"
                  -P "${CMAKE_CURRENT_LIST_DIR}/apply_patches.cmake"
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
