///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2016 - 2023 Vojtěch Bubník @bubnikv
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifdef WIN32
// Why?
#define _WIN32_WINNT 0x0502
// The standard Windows includes.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif // NOMINMAX
#include <Windows.h>
#include <wchar.h>
#ifdef SLIC3R_GUI
extern "C"
{
    // Let the NVIDIA and AMD know we want to use their graphics card
    // on a dual graphics card system.
    __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif /* SLIC3R_GUI */
#endif /* WIN32 */

#include <cstdio>
#include <string>
#include <cstring>
#include <iostream>
#include <vector>

#include <boost/nowide/args.hpp>

#include "libslic3r/libslic3r.h"

#include "preFlight.hpp"

#ifdef __linux__
#include <glib.h>
// Suppress known cosmetic GTK/GDK-CRITICAL assertions that fire when wxWidgets
// operates on widgets before they are fully realized or after they are destroyed.
// Must be installed before wxEntry() runs, as wxWidgets triggers these during its
// own initialization — well before wxApp::OnInit().
static void preflight_gtk_log_handler(const gchar * /*log_domain*/, GLogLevelFlags /*log_level*/, const gchar *message,
                                      gpointer /*user_data*/)
{
    if (message)
    {
        if (strstr(message, "gtk_widget_get_style_context") || strstr(message, "gtk_style_context_add_provider") ||
            strstr(message, "gtk_label_set_text_with_mnemonic") || strstr(message, "gtk_label_set_mnemonic_widget") ||
            strstr(message, "gtk_window_resize") || strstr(message, "gtk_grab_remove") ||
            strstr(message, "gtk_widget_get_display") || strstr(message, "gdk_device_manager_get_client_pointer") ||
            strstr(message, "gdk_device_ungrab") || strstr(message, "gdk_display_get_device_manager"))
            return;
    }
    g_log_default_handler(nullptr, G_LOG_LEVEL_CRITICAL, message, nullptr);
}
#endif

// __has_feature() is used later for Clang, this is for compatibility with other compilers (such as GCC and MSVC)
#ifndef __has_feature
#define __has_feature(x) 0
#endif

#if __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
extern "C"
{
    // Based on https://github.com/google/skia/blob/main/tools/LsanSuppressions.cpp
    const char *__lsan_default_suppressions()
    {
        return "leak:libfontconfig\n"           // FontConfig looks like it leaks, but it doesn't.
               "leak:libfreetype\n"             // Unsure, appeared upgrading Debian 9->10.
               "leak:libGLX_nvidia.so\n"        // For NVidia driver.
               "leak:libnvidia-glcore.so\n"     // For NVidia driver.
               "leak:libnvidia-tls.so\n"        // For NVidia driver.
               "leak:terminator_CreateDevice\n" // For Intel Vulkan drivers.
               "leak:swrast_dri.so\n"           // For Mesa 3D software driver.
               "leak:amdgpu_dri.so\n"           // For AMD driver.
               "leak:libdrm_amdgpu.so\n"        // For AMD driver.
               "leak:libdbus-1.so\n"            // For D-Bus library. Unsure if it is a leak or not.
            ;
    }
}
#endif

#if defined(SLIC3R_UBSAN)
extern "C"
{
    // Enable printing stacktrace by default. It can be disabled by running preFlight with "UBSAN_OPTIONS=print_stacktrace=0".
    const char *__ubsan_default_options()
    {
        return "print_stacktrace=1";
    }
}
#endif

#if defined(_MSC_VER) || defined(__MINGW32__)
extern "C"
{
    __declspec(dllexport) int __stdcall slic3r_main(int argc, wchar_t **argv)
    {
        // Convert wchar_t arguments to UTF8.
        std::vector<std::string> argv_narrow;
        std::vector<char *> argv_ptrs(argc + 1, nullptr);
        for (size_t i = 0; i < argc; ++i)
            argv_narrow.emplace_back(boost::nowide::narrow(argv[i]));
        for (size_t i = 0; i < argc; ++i)
            argv_ptrs[i] = argv_narrow[i].data();
        // Call the UTF8 main.
        return Slic3r::CLI::run(argc, argv_ptrs.data());
    }
}
#else /* _MSC_VER */
int main(int argc, char **argv)
{
#ifdef __linux__
    // preFlight: Force dark GTK theme on Linux until light mode theming is fully reworked
    setenv("GTK_THEME", "Adwaita:dark", 0); // 0 = don't override if user explicitly sets it

    // Install GTK/GDK log handlers before wxWidgets initialization to suppress
    // cosmetic CRITICAL assertions from styling unrealized widgets
    g_log_set_handler("Gtk", G_LOG_LEVEL_CRITICAL, preflight_gtk_log_handler, nullptr);
    g_log_set_handler("Gdk", G_LOG_LEVEL_CRITICAL, preflight_gtk_log_handler, nullptr);
#endif

    return Slic3r::CLI::run(argc, argv);
}
#endif /* _MSC_VER */
