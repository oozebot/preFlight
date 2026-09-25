set(_wx_toolkit "")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    option(DEP_WX_GTK3 "Build wxWidgets for GTK3 instead of GTK2" OFF)

    set(_gtk_ver 2)
    if (DEP_WX_GTK3)
        set(_gtk_ver 3)
    endif ()
    set(_wx_toolkit "-DwxBUILD_TOOLKIT=gtk${_gtk_ver}")
endif()

set(_unicode_utf8 OFF)
if (UNIX AND NOT APPLE) # wxWidgets will not use char as the underlying type for wxString unless its forced to.
    set (_unicode_utf8 ON)
endif()

if (MSVC)
    set(_wx_webview "-DwxUSE_WEBVIEW_EDGE=ON")

else ()
    set(_wx_webview "-DwxUSE_WEBVIEW=ON")
endif ()

if (UNIX AND NOT APPLE)
    set(_wx_secretstore "-DwxUSE_SECRETSTORE=OFF")
else ()
    set(_wx_secretstore "-DwxUSE_SECRETSTORE=ON")
endif ()

# Upstream wx 3.2.9 (stable branch), unpatched:
# - the macOS 14 CGDisplayCreateImage deprecation fix is included (no patch needed)
# NOTE: wx 3.3.1 has breaking changes for this code base (wxStaticBoxSizer, private headers), which
#       is why the 3.2 stable branch is used
add_cmake_project(wxWidgets
    URL https://github.com/wxWidgets/wxWidgets/releases/download/v3.2.9/wxWidgets-3.2.9.zip
    URL_HASH SHA256=cdfc198704c9f8faecf1f5fa6510c6e78f4562d8c803a51c97f5f71b01907b15
    CMAKE_ARGS
        "-DCMAKE_DEBUG_POSTFIX:STRING="
        -DwxBUILD_PRECOMP=ON
        ${_wx_toolkit}
        -DwxUSE_MEDIACTRL=OFF
        -DwxUSE_DETECT_SM=OFF
        -DwxUSE_UNICODE=ON
        -DwxUSE_UNICODE_UTF8=${_unicode_utf8}
        -DwxUSE_OPENGL=ON
        -DwxUSE_LIBPNG=sys
        -DwxUSE_ZLIB=sys
        -DwxUSE_NANOSVG=sys
        -DwxUSE_NANOSVG_EXTERNAL=ON
        -DwxUSE_REGEX=OFF
        -DwxUSE_LIBXPM=builtin
        -DwxUSE_LIBJPEG=sys
        -DwxUSE_LIBTIFF=OFF
        -DwxUSE_EXPAT=sys
        -DwxUSE_LIBSDL=OFF
        -DwxUSE_XTEST=OFF
        -DwxUSE_GLCANVAS_EGL=OFF
        -DwxUSE_WEBREQUEST=OFF
        ${_wx_webview}
        ${_wx_secretstore}
)

set(DEP_wxWidgets_DEPENDS ZLIB PNG EXPAT JPEG NanoSVG)


if (MSVC)
    # Determine the wxWidgets lib directory name based on target architecture
    if(CMAKE_GENERATOR_PLATFORM STREQUAL "ARM64")
        set(_wx_lib_dir "vc_arm64_lib")
    else()
        set(_wx_lib_dir "vc_x64_lib")
    endif()

    # Copy WebView2Loader.dll into the installation directory
    add_custom_command(TARGET dep_wxWidgets POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_BINARY_DIR}/builds/wxWidgets/lib/${_wx_lib_dir}/WebView2Loader.dll"
            "${${PROJECT_NAME}_DEP_INSTALL_PREFIX}/bin/WebView2Loader.dll")
endif()

