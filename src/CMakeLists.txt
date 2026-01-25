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
if(SLIC3R_STATIC)
    slic3r_remap_configs("Qhull::qhullcpp;Qhull::qhullstatic_r" RelWithDebInfo Release)
    target_link_libraries(qhull INTERFACE Qhull::qhullcpp Qhull::qhullstatic_r)
else()
    slic3r_remap_configs("Qhull::qhullcpp;Qhull::qhull_r" RelWithDebInfo Release)
    target_link_libraries(qhull INTERFACE Qhull::qhullcpp Qhull::qhull_r)
endif()

add_subdirectory(libslic3r)

if (SLIC3R_ENABLE_FORMAT_STEP)
    add_subdirectory(occt_wrapper)
endif ()

add_subdirectory(slic3r-arrange)
add_subdirectory(slic3r-arrange-wrapper)
add_subdirectory(libseqarrange)


if (SLIC3R_GUI)
    add_subdirectory(libvgcode)

    if(WIN32)
        message(STATUS "WXWIN environment set to: $ENV{WXWIN}")
    elseif(UNIX)
        set(wxWidgets_USE_UNICODE ON)
        if(SLIC3R_STATIC)
            set(wxWidgets_USE_STATIC ON)
        else()
            set(wxWidgets_USE_STATIC OFF)
        endif()
    endif()

    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set (wxWidgets_CONFIG_OPTIONS "--toolkit=gtk${SLIC3R_GTK}")
    endif ()
    find_package(wxWidgets 3.2 MODULE REQUIRED COMPONENTS base core adv html gl webview)

    include(${wxWidgets_USE_FILE})

    slic3r_remap_configs(wx::wxhtml wx::wxadv wx::wxgl wx::wxcore wx::wxbase RelWithDebInfo Release)

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
    # libslic3r_gui will link to opengl anyway, so lets override wx
    list(FILTER wxWidgets_LIBRARIES EXCLUDE REGEX OpenGL)
    
    if (UNIX AND NOT APPLE)
        list(APPEND wxWidgets_LIBRARIES X11 wayland-client wayland-egl EGL)
    endif ()
#    list(REMOVE_ITEM wxWidgets_LIBRARIES oleacc)
    message(STATUS "wx libs: ${wxWidgets_LIBRARIES}")

    add_subdirectory(slic3r)
endif()


# Create a slic3r executable
# Process mainfests for various platforms.
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/platform/msw/preFlight.rc.in ${CMAKE_CURRENT_BINARY_DIR}/preFlight.rc @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/platform/msw/preFlight-gcodeviewer.rc.in ${CMAKE_CURRENT_BINARY_DIR}/preFlight-gcodeviewer.rc @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/platform/msw/preFlight.manifest.in ${CMAKE_CURRENT_BINARY_DIR}/preFlight.manifest @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/platform/osx/Info.plist.in ${CMAKE_CURRENT_BINARY_DIR}/Info.plist @ONLY)

set(SLIC3R_CLI_SOURCES
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
if (SLIC3R_GUI)
    list(APPEND SLIC3R_CLI_SOURCES
         CLI/GuiParams.cpp
    )
endif ()

if (WIN32)
    add_library(preFlight SHARED preFlight.cpp ${SLIC3R_CLI_SOURCES})
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
    add_executable(preFlight preFlight.cpp ${SLIC3R_CLI_SOURCES})
endif ()

foreach(_source IN ITEMS ${SLIC3R_CLI_SOURCES})
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


target_link_libraries(preFlight PRIVATE libslic3r libcereal slic3r-arrange-wrapper libseqarrange stb_image)

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

# Add the Slic3r GUI library, libcurl, OpenGL and GLU libraries.
if (SLIC3R_GUI)
    target_link_libraries(preFlight PRIVATE libslic3r_gui)
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

# On Windows, a shim application is required to produce a console / non console version of the Slic3r application.
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
    target_compile_definitions(preFlight_app_gui PRIVATE -DSLIC3R_WRAPPER_NOCONSOLE)
    add_dependencies(preFlight_app_gui preFlight)
    set_target_properties(preFlight_app_gui PROPERTIES OUTPUT_NAME "preFlight")
    target_link_libraries(preFlight_app_gui PRIVATE boost_headeronly)

    add_executable(preFlight_app_console preFlight_app_msvc.cpp ${CMAKE_CURRENT_BINARY_DIR}/preFlight.rc)
    # Generate debug symbols even in release mode.
    if (MSVC)
        target_link_options(preFlight_app_console PUBLIC "$<$<CONFIG:RELEASE>:/DEBUG>")
    endif ()
    target_compile_definitions(preFlight_app_console PRIVATE -DSLIC3R_WRAPPER_CONSOLE)
    add_dependencies(preFlight_app_console preFlight)
    set_target_properties(preFlight_app_console PROPERTIES OUTPUT_NAME "preFlight-console")
    target_link_libraries(preFlight_app_console PRIVATE boost_headeronly)

    add_executable(preFlight_app_gcodeviewer WIN32 preFlight_app_msvc.cpp ${CMAKE_CURRENT_BINARY_DIR}/preFlight-gcodeviewer.rc)
    # Generate debug symbols even in release mode.
    if (MSVC)
        target_link_options(preFlight_app_gcodeviewer PUBLIC "$<$<CONFIG:RELEASE>:/DEBUG>")
    endif ()
    target_compile_definitions(preFlight_app_gcodeviewer PRIVATE -DSLIC3R_WRAPPER_NOCONSOLE -DSLIC3R_WRAPPER_GCODEVIEWER)
    add_dependencies(preFlight_app_gcodeviewer preFlight)
    set_target_properties(preFlight_app_gcodeviewer PROPERTIES OUTPUT_NAME "preFlight-gcodeviewer")
    target_link_libraries(preFlight_app_gcodeviewer PRIVATE boost_headeronly)

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

# Link the resources dir to where Slic3r GUI expects it
if (WIN32)
    if (CMAKE_CONFIGURATION_TYPES)
        foreach (CONF ${CMAKE_CONFIGURATION_TYPES})
            file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/${CONF}" WIN_CONF_OUTPUT_DIR)
            file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/${CONF}/resources" WIN_RESOURCES_SYMLINK)
            add_custom_command(TARGET preFlight POST_BUILD
                COMMAND if exist "${WIN_CONF_OUTPUT_DIR}" "("
                        if not exist "${WIN_RESOURCES_SYMLINK}" "("
                            mklink /J "${WIN_RESOURCES_SYMLINK}" "${SLIC3R_RESOURCES_DIR_WIN}"
                        ")"
                    ")"
                COMMENT "Symlinking the resources directory into the build tree"
                VERBATIM
            )

            if (SLIC3R_GUI)
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
                            mklink /J "${WIN_RESOURCES_SYMLINK}" "${SLIC3R_RESOURCES_DIR_WIN}"
                        ")"
                    ")"
            COMMENT "Symlinking the resources directory into the build tree"
            VERBATIM
        )
        if (SLIC3R_GUI)
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
        # On OSX, the name of the binary matches the name of the Application.
        add_custom_command(TARGET preFlight POST_BUILD
            COMMAND ln -sf preFlight preflight
            COMMAND ln -sf preFlight preflight-gcodeviewer
            COMMAND ln -sf preFlight preFlightGCodeViewer
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:preFlight>"
            COMMENT "Symlinking the G-code viewer to preFlight, symlinking to preflight and preflight-gcodeviewer"
            VERBATIM)
    else ()
        add_custom_command(TARGET preFlight POST_BUILD
            COMMAND ln -sf preflight preflight-gcodeviewer
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:preFlight>"
            COMMENT "Symlinking the G-code viewer to preFlight"
            VERBATIM)
    endif ()
    if (XCODE)
        # Because of Debug/Release/etc. configurations (similar to MSVC) the slic3r binary is located in an extra level
        set(BIN_RESOURCES_DIR "${CMAKE_CURRENT_BINARY_DIR}/resources")
    else ()
        set(BIN_RESOURCES_DIR "${CMAKE_CURRENT_BINARY_DIR}/../resources")
    endif ()
    add_custom_command(TARGET preFlight POST_BUILD
        COMMAND ln -sfn "${SLIC3R_RESOURCES_DIR}" "${BIN_RESOURCES_DIR}"
        COMMENT "Symlinking the resources directory into the build tree"
        VERBATIM)
endif ()

# Slic3r binary install target
if (WIN32)
    install(TARGETS preFlight RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}")
    if (MSVC)
        install(TARGETS preFlight_app_gui RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}")
        install(TARGETS preFlight_app_console RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}")
    endif ()
else ()
    install(TARGETS preFlight RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

    # Install the symlink for gcodeviewer
    install(CODE "execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink preflight preflight-gcodeviewer WORKING_DIRECTORY \$ENV{DESTDIR}/${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR})")
endif ()
