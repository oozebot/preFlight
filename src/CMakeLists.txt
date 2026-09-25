#/|/ Copyright (c) preFlight 2025+ oozeBot, LLC
#/|/
#/|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
#/|/
cmake_minimum_required(VERSION 3.13)
project(preFlight-native)

# preFlight uses Clipper2 for all polygon operations
add_subdirectory(clipper2)
message(STATUS "preFlight: Using Clipper2 exclusively")
find_package(Qhull 7.2 REQUIRED)
add_library(qhull INTERFACE)
if(PREFLIGHT_STATIC)
    preflight_remap_configs("Qhull::qhullcpp;Qhull::qhullstatic_r" RelWithDebInfo Release)
    target_link_libraries(qhull INTERFACE Qhull::qhullcpp Qhull::qhullstatic_r)
else()
    preflight_remap_configs("Qhull::qhullcpp;Qhull::qhull_r" RelWithDebInfo Release)
    target_link_libraries(qhull INTERFACE Qhull::qhullcpp Qhull::qhull_r)
endif()

add_subdirectory(luminary)

# Build-target parity: every target that compiles engine sources must carry the shared
# preflight_engine_flags definitions, and must add no public definition of its own that the shared
# set lacks, so a header compiled in one of them sees the same class layouts as the same header
# compiled in another. A definition that belongs to one target only is a silent ABI split inside one
# DLL: PREFLIGHT_PYTHON_PREPROCESSOR adds five data members to class Print inside Print.hpp, and no
# output gate can see two targets disagreeing about that. The check covers the link as well as the
# definitions, since a target can carry none of the shared definitions by not linking the shared set
# at all.
get_target_property(_pf_shared_defs preflight_engine_flags INTERFACE_COMPILE_DEFINITIONS)
foreach(_pf_engine_target luminary luminary_cgal)
    get_target_property(_pf_links ${_pf_engine_target} LINK_LIBRARIES)
    if (NOT _pf_links OR NOT "preflight_engine_flags" IN_LIST _pf_links)
        message(FATAL_ERROR "Engine compile-definition parity: ${_pf_engine_target} compiles engine sources but "
                            "does not link preflight_engine_flags, so it carries none of the shared definitions "
                            "(src/luminary/CMakeLists.txt).")
    endif ()
    # An unset property reads back as <var>-NOTFOUND, which would look like one definition named
    # that, so a target with no public definitions is skipped rather than failed.
    get_target_property(_pf_target_defs ${_pf_engine_target} INTERFACE_COMPILE_DEFINITIONS)
    if (_pf_target_defs)
        foreach(_def ${_pf_target_defs})
            string(REGEX REPLACE "^-D" "" _def_clean "${_def}")
            if (NOT _def_clean IN_LIST _pf_shared_defs)
                message(FATAL_ERROR "Engine compile-definition parity: ${_pf_engine_target} sets '${_def_clean}' "
                                    "publicly but preflight_engine_flags does not carry it "
                                    "(src/luminary/CMakeLists.txt).")
            endif ()
        endforeach()
    endif ()
endforeach()

if (PREFLIGHT_ENABLE_FORMAT_STEP)
    add_subdirectory(step)
endif ()

if (PREFLIGHT_GUI)
    add_subdirectory(viewer)

    if(WIN32)
        message(STATUS "WXWIN environment set to: $ENV{WXWIN}")
    elseif(UNIX)
        set(wxWidgets_USE_UNICODE ON)
        if(PREFLIGHT_STATIC)
            set(wxWidgets_USE_STATIC ON)
        else()
            set(wxWidgets_USE_STATIC OFF)
        endif()
    endif()

    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set (wxWidgets_CONFIG_OPTIONS "--toolkit=gtk${PREFLIGHT_GTK}")
    endif ()
    find_package(wxWidgets 3.2 MODULE REQUIRED COMPONENTS base core adv html gl webview)

    include(${wxWidgets_USE_FILE})

    preflight_remap_configs(wx::wxhtml wx::wxadv wx::wxgl wx::wxcore wx::wxbase RelWithDebInfo Release)

    if(UNIX)
        message(STATUS "wx-config path: ${wxWidgets_CONFIG_EXECUTABLE}")
    endif()

    find_package(JPEG MODULE QUIET)
    find_package(NanoSVG REQUIRED)

    string(REGEX MATCH "wxpng" WX_PNG_BUILTIN ${wxWidgets_LIBRARIES})
    if (PNG_FOUND AND NOT WX_PNG_BUILTIN)
        list(FILTER wxWidgets_LIBRARIES EXCLUDE REGEX png)
        list(APPEND wxWidgets_LIBRARIES ${PNG_LIBRARIES})
    endif ()

    string(REGEX MATCH "wxjpeg" WX_JPEG_BUILTIN ${wxWidgets_LIBRARIES})
    if (JPEG_FOUND AND NOT WX_JPEG_BUILTIN)
        list(FILTER wxWidgets_LIBRARIES EXCLUDE REGEX jpeg)
        list(APPEND wxWidgets_LIBRARIES ${JPEG_LIBRARIES})
    endif ()

    string(REGEX MATCH "wxexpat" WX_EXPAT_BUILTIN ${wxWidgets_LIBRARIES})
    if (EXPAT_FOUND AND NOT WX_EXPAT_BUILTIN)
        list(FILTER wxWidgets_LIBRARIES EXCLUDE REGEX expat)
        list(APPEND wxWidgets_LIBRARIES libexpat)
    endif ()
    
    # This is an issue in the new wxWidgets cmake build, doesn't deal with librt
    find_library(LIBRT rt)
    if(LIBRT)
        list(APPEND wxWidgets_LIBRARIES ${LIBRT})
    endif()

    # This fixes a OpenGL linking issue on OSX. wxWidgets cmake build includes
    # wrong libs for opengl in the link line and it does not link to it by himself.
    # dsky will link to opengl anyway, so lets override wx
    list(FILTER wxWidgets_LIBRARIES EXCLUDE REGEX OpenGL)
    
    if (UNIX AND NOT APPLE)
        list(APPEND wxWidgets_LIBRARIES X11 wayland-client wayland-egl EGL)
    endif ()
#    list(REMOVE_ITEM wxWidgets_LIBRARIES oleacc)
    message(STATUS "wx libs: ${wxWidgets_LIBRARIES}")

    add_subdirectory(DSKY)
endif()


# Create the preFlight executable
# Process mainfests for various platforms.
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/platform/msw/preFlight.rc.in ${CMAKE_CURRENT_BINARY_DIR}/preFlight.rc @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/platform/msw/preFlight-gcodeviewer.rc.in ${CMAKE_CURRENT_BINARY_DIR}/preFlight-gcodeviewer.rc @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/platform/msw/preFlight.manifest.in ${CMAKE_CURRENT_BINARY_DIR}/preFlight.manifest @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/platform/osx/Info.plist.in ${CMAKE_CURRENT_BINARY_DIR}/Info.plist @ONLY)

set(PREFLIGHT_CLI_SOURCES
    preFlight.hpp
    CLI/CLI.hpp
    CLI/CLI_DynamicPrintConfig.hpp
    CLI/PrintHelp.cpp
    CLI/Setup.cpp
    CLI/LoadPrintData.cpp
    CLI/ProcessTransform.cpp
    CLI/ProcessActions.cpp
    CLI/Run.cpp
    CLI/ProfilesSharingUtils.cpp
    CLI/ProfilesSharingUtils.hpp
)
if (PREFLIGHT_GUI)
    list(APPEND PREFLIGHT_CLI_SOURCES
         CLI/GuiParams.cpp
    )
endif ()

if (WIN32)
    add_library(preFlight SHARED preFlight.cpp ${PREFLIGHT_CLI_SOURCES})
    set_target_properties(preFlight PROPERTIES OUTPUT_NAME "preFlight")
    # Use unique PDB names to avoid conflict between DLL and EXE
    if (MSVC)
        set_target_properties(preFlight PROPERTIES
            COMPILE_PDB_NAME "preFlight_dll"
            COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            PDB_NAME "preFlight_dll"
            PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>"
        )
    endif()
else ()
    add_executable(preFlight preFlight.cpp ${PREFLIGHT_CLI_SOURCES})
endif ()

foreach(_source IN ITEMS ${PREFLIGHT_CLI_SOURCES})
    get_filename_component(_source_path "${_source}" PATH)
    string(REPLACE "/" "\\" _group_path "${_source_path}")
    source_group("${_group_path}" FILES "${_source}")
endforeach()

if (MINGW)
    target_link_options(preFlight PUBLIC "-Wl,-allow-multiple-definition")
    set_target_properties(preFlight PROPERTIES PREFIX "")
endif (MINGW)

if (NOT WIN32 AND NOT APPLE)
    # Binary name on unix like systems (Linux, Unix)
    set_target_properties(preFlight PROPERTIES OUTPUT_NAME "preflight")
endif ()

# RPATH for bundled Python: the preFlight binary needs to find libpython3.XX.so
# in the bundled python/lib/ directory at runtime (no system Python required).
if (PREFLIGHT_PYTHON_PREPROCESSOR AND NOT WIN32)
    if (APPLE)
        set_target_properties(preFlight PROPERTIES
            INSTALL_RPATH "@loader_path/../python/lib"
            BUILD_WITH_INSTALL_RPATH TRUE)
    else ()
        set_target_properties(preFlight PROPERTIES
            INSTALL_RPATH "$ORIGIN/../python/lib"
            BUILD_WITH_INSTALL_RPATH TRUE)
    endif ()
endif ()

target_link_libraries(preFlight PRIVATE luminary libcereal stb_image)

# The proxy patches the CRT heap only when it is loaded statically (its DllMain checks the
# load reason), so it must be linked into the shim executables; preFlight.dll is loaded via
# LoadLibrary and a proxy imported there never engages. The /INCLUDE forces the otherwise
# unreferenced proxy symbol so the linker keeps the import.
function(preflight_link_tbbmalloc_proxy target)
    if (WIN32 AND PREFLIGHT_TBBMALLOC_PROXY)
        # The deps build installs the shared malloc libraries into the isolated
        # tbbmalloc-proxy/ prefix subdirectory (see deps/+TBBMallocProxy).
        find_library(TBBMALLOC_PROXY_LIBRARY NAMES tbbmalloc_proxy
                     PATH_SUFFIXES tbbmalloc-proxy/lib REQUIRED)
        target_link_libraries(${target} PRIVATE ${TBBMALLOC_PROXY_LIBRARY})
        target_link_options(${target} PRIVATE "/INCLUDE:__TBB_malloc_proxy")
    endif ()
endfunction()

if (APPLE)
#    add_compile_options(-stdlib=libc++)
#    add_definitions(-DBOOST_THREAD_DONT_USE_CHRONO -DBOOST_NO_CXX11_RVALUE_REFERENCES -DBOOST_THREAD_USES_MOVE)
    # -liconv: boost links to libiconv by default
    target_link_libraries(preFlight PRIVATE "-liconv -framework IOKit" "-framework CoreFoundation" -lc++)
elseif (MSVC)
    # Manifest is provided through preFlight.rc, don't generate your own.
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /MANIFEST:NO")
else ()
    # Boost on Raspberry-Pi does not link to pthreads explicitely.
    target_link_libraries(preFlight PRIVATE ${CMAKE_DL_LIBS} -lstdc++ Threads::Threads)
endif ()

# Add the DSKY GUI library, libcurl, OpenGL and GLU libraries.
if (PREFLIGHT_GUI)
    target_link_libraries(preFlight PRIVATE dsky)
    if (MSVC)
        # Generate debug symbols even in release mode.
        target_link_options(preFlight PUBLIC "$<$<CONFIG:RELEASE>:/DEBUG>")
        target_link_libraries(preFlight PRIVATE user32.lib Setupapi.lib)
    elseif (MINGW)
        target_link_libraries(preFlight PRIVATE ws2_32 uxtheme setupapi)
    elseif (APPLE)
        target_link_libraries(preFlight PRIVATE "-framework OpenGL")
    else ()
        target_link_libraries(preFlight PRIVATE -ldl)
    endif ()
    if (WIN32)
        find_library(PSAPI_LIB NAMES Psapi)
        target_link_libraries(preFlight PRIVATE ${PSAPI_LIB})
    endif ()
endif ()

# On Windows, a shim application is required to produce a console / non console version of the preFlight application.
# Also the shim may load the Mesa software OpenGL renderer if the default renderer does not support OpenGL 2.0 and higher.
if (WIN32)
    if (MINGW)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -municode")
    endif()

    add_executable(preFlight_app_gui WIN32 preFlight_app_msvc.cpp ${CMAKE_CURRENT_BINARY_DIR}/preFlight.rc)
    # Generate debug symbols even in release mode.
    if(MSVC)
        target_link_options(preFlight_app_gui PUBLIC "$<$<CONFIG:RELEASE>:/DEBUG>")
        # Use unique PDB name to avoid conflict with preFlight.dll
        set_target_properties(preFlight_app_gui PROPERTIES
            COMPILE_PDB_NAME "preFlight_exe"
            PDB_NAME "preFlight_exe"
        )
    endif()
    target_compile_definitions(preFlight_app_gui PRIVATE -DPREFLIGHT_WRAPPER_NOCONSOLE)
    add_dependencies(preFlight_app_gui preFlight)
    set_target_properties(preFlight_app_gui PROPERTIES OUTPUT_NAME "preFlight")
    target_link_libraries(preFlight_app_gui PRIVATE boost_headeronly)
    preflight_link_tbbmalloc_proxy(preFlight_app_gui)

    add_executable(preFlight_app_console preFlight_app_msvc.cpp ${CMAKE_CURRENT_BINARY_DIR}/preFlight.rc)
    # Generate debug symbols even in release mode.
    if (MSVC)
        target_link_options(preFlight_app_console PUBLIC "$<$<CONFIG:RELEASE>:/DEBUG>")
    endif ()
    target_compile_definitions(preFlight_app_console PRIVATE -DPREFLIGHT_WRAPPER_CONSOLE)
    add_dependencies(preFlight_app_console preFlight)
    set_target_properties(preFlight_app_console PROPERTIES OUTPUT_NAME "preFlight-console")
    target_link_libraries(preFlight_app_console PRIVATE boost_headeronly)
    preflight_link_tbbmalloc_proxy(preFlight_app_console)

    add_executable(preFlight_app_gcodeviewer WIN32 preFlight_app_msvc.cpp ${CMAKE_CURRENT_BINARY_DIR}/preFlight-gcodeviewer.rc)
    # Generate debug symbols even in release mode.
    if (MSVC)
        target_link_options(preFlight_app_gcodeviewer PUBLIC "$<$<CONFIG:RELEASE>:/DEBUG>")
    endif ()
    target_compile_definitions(preFlight_app_gcodeviewer PRIVATE -DPREFLIGHT_WRAPPER_NOCONSOLE -DPREFLIGHT_WRAPPER_GCODEVIEWER)
    add_dependencies(preFlight_app_gcodeviewer preFlight)
    set_target_properties(preFlight_app_gcodeviewer PROPERTIES OUTPUT_NAME "preFlight-gcodeviewer")
    target_link_libraries(preFlight_app_gcodeviewer PRIVATE boost_headeronly)
    preflight_link_tbbmalloc_proxy(preFlight_app_gcodeviewer)

    if (PREFLIGHT_TBBMALLOC_PROXY)
        # The executables import tbbmalloc_proxy.dll, so it and its tbbmalloc.dll must
        # sit next to them. All three shims share one output directory; copy once.
        get_filename_component(_tbbmalloc_proxy_libdir "${TBBMALLOC_PROXY_LIBRARY}" DIRECTORY)
        find_file(TBBMALLOC_DLL NAMES tbbmalloc.dll
                  HINTS "${_tbbmalloc_proxy_libdir}/../bin" "${_tbbmalloc_proxy_libdir}" REQUIRED)
        find_file(TBBMALLOC_PROXY_DLL NAMES tbbmalloc_proxy.dll
                  HINTS "${_tbbmalloc_proxy_libdir}/../bin" "${_tbbmalloc_proxy_libdir}" REQUIRED)
        add_custom_command(TARGET preFlight_app_console POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${TBBMALLOC_DLL}" "${TBBMALLOC_PROXY_DLL}"
                "$<TARGET_FILE_DIR:preFlight_app_console>")
    endif ()

    # Match MSBuild output path for single-config generators (Ninja)
    if (NOT CMAKE_CONFIGURATION_TYPES)
        set(_output_dir "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_BUILD_TYPE}")
        set_target_properties(preFlight PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${_output_dir}"
            LIBRARY_OUTPUT_DIRECTORY "${_output_dir}"
        )
        set_target_properties(preFlight_app_gui PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${_output_dir}")
        set_target_properties(preFlight_app_console PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${_output_dir}")
        set_target_properties(preFlight_app_gcodeviewer PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${_output_dir}")
    endif ()
endif ()

# Link the resources dir to where the GUI expects it
if (WIN32)
    if (CMAKE_CONFIGURATION_TYPES)
        foreach (CONF ${CMAKE_CONFIGURATION_TYPES})
            file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/${CONF}" WIN_CONF_OUTPUT_DIR)
            file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/${CONF}/resources" WIN_RESOURCES_SYMLINK)
            add_custom_command(TARGET preFlight POST_BUILD
                COMMAND if exist "${WIN_CONF_OUTPUT_DIR}" "("
                        if not exist "${WIN_RESOURCES_SYMLINK}" "("
                            mklink /J "${WIN_RESOURCES_SYMLINK}" "${PREFLIGHT_RESOURCES_DIR_WIN}"
                        ")"
                    ")"
                COMMENT "Symlinking the resources directory into the build tree"
                VERBATIM
            )

            if (PREFLIGHT_GUI)
                add_custom_command(TARGET preFlight POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy
                    "${wxWidgets_ROOT_DIR}/bin/WebView2Loader.dll"
                    "${CMAKE_CURRENT_BINARY_DIR}/${CONF}/WebView2Loader.dll")
            endif()
        endforeach ()
    else ()
        file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_BUILD_TYPE}" WIN_CONF_OUTPUT_DIR)
        file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_BUILD_TYPE}/resources" WIN_RESOURCES_SYMLINK)
        add_custom_command(TARGET preFlight POST_BUILD
            COMMAND if exist "${WIN_CONF_OUTPUT_DIR}" "("
                        if not exist "${WIN_RESOURCES_SYMLINK}" "("
                            mklink /J "${WIN_RESOURCES_SYMLINK}" "${PREFLIGHT_RESOURCES_DIR_WIN}"
                        ")"
                    ")"
            COMMENT "Symlinking the resources directory into the build tree"
            VERBATIM
        )
        if (PREFLIGHT_GUI)
            add_custom_command(TARGET preFlight POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "${wxWidgets_ROOT_DIR}/bin/WebView2Loader.dll"
                "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_BUILD_TYPE}/WebView2Loader.dll")
        endif()
    endif ()

    # This has to be a separate target due to the windows command line lenght limits
    add_custom_target(preFlightDllsCopy ALL DEPENDS preFlight)
    preflight_copy_dlls(preFlightDllsCopy)

else ()
    if (APPLE)
        # On macOS, the binary is "preFlight" (mixed-case).  Default APFS is
        # case-insensitive, so "preflight" and "preFlight" are the same file;
        # skip that symlink to avoid a self-referential loop.  Only create the
        # gcodeviewer symlinks whose names differ enough to be safe.
        add_custom_command(TARGET preFlight POST_BUILD
            COMMAND ln -sf preFlight preflight-gcodeviewer
            COMMAND ln -sf preFlight preFlightGCodeViewer
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:preFlight>"
            COMMENT "Symlinking the G-code viewer to preFlight"
            VERBATIM)
    else ()
        add_custom_command(TARGET preFlight POST_BUILD
            COMMAND ln -sf preflight preflight-gcodeviewer
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:preFlight>"
            COMMENT "Symlinking the G-code viewer to preFlight"
            VERBATIM)
    endif ()
    if (XCODE)
        # Because of Debug/Release/etc. configurations (similar to MSVC) the preFlight binary is located in an extra level
        set(BIN_RESOURCES_DIR "${CMAKE_CURRENT_BINARY_DIR}/resources")
    else ()
        set(BIN_RESOURCES_DIR "${CMAKE_CURRENT_BINARY_DIR}/../resources")
    endif ()
    add_custom_command(TARGET preFlight POST_BUILD
        COMMAND ln -sfn "${PREFLIGHT_RESOURCES_DIR}" "${BIN_RESOURCES_DIR}"
        COMMENT "Symlinking the resources directory into the build tree"
        VERBATIM)
endif ()

# Bundled Python runtime: copy to build output so the embedded interpreter works
# without a system Python installation.
if (PREFLIGHT_PYTHON_PREPROCESSOR AND PYTHON_RUNTIME_AVAILABLE)
    # Determine the Python version-specific filenames (e.g. python314 for 3.14)
    string(REPLACE "." "" _py_ver_nodot "${Python3_VERSION_MAJOR}${Python3_VERSION_MINOR}")

    if (WIN32)
        # Windows layout:
        #   <exe_dir>/python<ver>.dll   - main runtime DLL (OS loader needs it here)
        #   <exe_dir>/python3.dll       - stable ABI forwarder
        #   <exe_dir>/python/           - stdlib zip, .pyd extensions, python.exe
        # The DLLs exist in BOTH locations: exe dir for the linked preFlight binary,
        # python/ so standalone tools (pip.exe) load the DLL from python/ and find
        # the ._pth file there for proper isolation from system Python.
        if (CMAKE_CONFIGURATION_TYPES)
            foreach (CONF ${CMAKE_CONFIGURATION_TYPES})
                set(_out "${CMAKE_CURRENT_BINARY_DIR}/${CONF}")
                add_custom_command(TARGET preFlight POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${PYTHON_RUNTIME_DIR}/python${_py_ver_nodot}.dll"
                        "${_out}/python${_py_ver_nodot}.dll"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${PYTHON_RUNTIME_DIR}/python3.dll"
                        "${_out}/python3.dll"
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${_out}/python"
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${PYTHON_RUNTIME_DIR}"
                        "${_out}/python"
                    COMMAND ${CMAKE_COMMAND}
                        -DPYTHON_DIR=${_out}/python
                        -P ${CMAKE_SOURCE_DIR}/cmake/modules/DownloadGetPip.cmake
                    COMMENT "Bundling Python runtime to ${CONF}/python/"
                    VERBATIM
                )
            endforeach ()
        else ()
            set(_out "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_BUILD_TYPE}")
            add_custom_command(TARGET preFlight POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${PYTHON_RUNTIME_DIR}/python${_py_ver_nodot}.dll"
                    "${_out}/python${_py_ver_nodot}.dll"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${PYTHON_RUNTIME_DIR}/python3.dll"
                    "${_out}/python3.dll"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${_out}/python"
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${PYTHON_RUNTIME_DIR}"
                    "${_out}/python"
                COMMAND ${CMAKE_COMMAND}
                    -DPYTHON_DIR=${_out}/python
                    -P ${CMAKE_SOURCE_DIR}/cmake/modules/DownloadGetPip.cmake
                COMMENT "Bundling Python runtime to ${CMAKE_BUILD_TYPE}/python/"
                VERBATIM
            )
        endif ()
    elseif (APPLE)
        # macOS layout:
        #   <exe_dir>/../python/        - shared lib, stdlib, extensions
        # After copying, fix dylib references so the binary uses the bundled
        # libpython via @rpath instead of the build-time absolute path.
        set(_py_dylib_name "libpython${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}.dylib")
        get_target_property(_py_lib_location Python3::Python IMPORTED_LOCATION)
        if (NOT _py_lib_location)
            # Framework builds store it in IMPORTED_LOCATION_RELEASE
            get_target_property(_py_lib_location Python3::Python IMPORTED_LOCATION_RELEASE)
        endif ()
        # Get the install_name from the Python library (what the linker embeds)
        execute_process(
            COMMAND otool -D "${_py_lib_location}"
            OUTPUT_VARIABLE _py_install_name
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        # otool -D output: first line is the filename, second line is the install_name
        string(REGEX REPLACE "^[^\n]*\n" "" _py_install_name "${_py_install_name}")
        message(STATUS "Python library install_name: ${_py_install_name}")
        add_custom_command(TARGET preFlight POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:preFlight>/../python"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${PYTHON_RUNTIME_DIR}"
                "$<TARGET_FILE_DIR:preFlight>/../python"
            COMMAND install_name_tool -id "@rpath/${_py_dylib_name}"
                "$<TARGET_FILE_DIR:preFlight>/../python/lib/${_py_dylib_name}"
            COMMAND install_name_tool -change "${_py_install_name}"
                "@rpath/${_py_dylib_name}"
                "$<TARGET_FILE:preFlight>"
            COMMENT "Bundling Python runtime"
            VERBATIM
        )
    else ()
        # Linux layout:
        #   <exe_dir>/../python/        - shared lib, stdlib, extensions
        add_custom_command(TARGET preFlight POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:preFlight>/../python"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${PYTHON_RUNTIME_DIR}"
                "$<TARGET_FILE_DIR:preFlight>/../python"
            COMMENT "Bundling Python runtime"
            VERBATIM
        )
    endif ()
endif ()

# preFlight binary install target
if (WIN32)
    install(TARGETS preFlight RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}")
    if (MSVC)
        install(TARGETS preFlight_app_gui RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}")
        install(TARGETS preFlight_app_console RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}")
    endif ()
    if (PREFLIGHT_PYTHON_PREPROCESSOR AND PYTHON_RUNTIME_AVAILABLE)
        string(REPLACE "." "" _py_ver_nodot "${Python3_VERSION_MAJOR}${Python3_VERSION_MINOR}")
        set(_py_ver_dotted "${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}")
        install(FILES "${PYTHON_RUNTIME_DIR}/python${_py_ver_nodot}.dll" DESTINATION "${CMAKE_INSTALL_PREFIX}")
        install(FILES "${PYTHON_RUNTIME_DIR}/python3.dll" DESTINATION "${CMAKE_INSTALL_PREFIX}")
        install(DIRECTORY "${PYTHON_RUNTIME_DIR}/" DESTINATION "${CMAKE_INSTALL_PREFIX}/python")
    endif ()
else ()
    install(TARGETS preFlight RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

    # Install the symlink for gcodeviewer
    install(CODE "execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink preflight preflight-gcodeviewer WORKING_DIRECTORY \$ENV{DESTDIR}/${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR})")

    if (PREFLIGHT_PYTHON_PREPROCESSOR AND PYTHON_RUNTIME_AVAILABLE)
        install(DIRECTORY "${PYTHON_RUNTIME_DIR}/" DESTINATION "${CMAKE_INSTALL_PREFIX}/python")
    endif ()
endif ()
