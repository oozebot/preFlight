///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2016 - 2023 Pavel Mikuš @Godrak, Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Filip Sykala @Jony01, David Kocík @kocikdav, Roman Beránek @zavorka, Enrico Turri @enricoturri1966, Tomáš Mészáros @tamasmeszaros, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2021 Justin Schuh @jschuh
///|/ Copyright (c) Slic3r 2013 - 2015 Alessandro Ranellucci @alranel
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "luminary/platform/paths/Paths.hpp"

#include "luminary/core/Prelude.hpp"

#include <atomic>
#include <cstdarg>
#include <cstdio>

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/log/trivial.hpp>
// Named here rather than inherited from a precompiled header: inside src/luminary there is none.
#include <boost/nowide/convert.hpp>

// is_shapes_dir is declared with the file predicates and defined here, beside the registry it reads.
#include "luminary/platform/files/FileIO.hpp"

namespace Luminary
{

static std::string g_var_dir;

void set_var_dir(const std::string &dir)
{
    g_var_dir = dir;
}

const std::string &var_dir()
{
    return g_var_dir;
}

std::string var(const std::string &file_name)
{
    auto file = (boost::filesystem::path(g_var_dir) / file_name).make_preferred();
    return file.string();
}

static std::string g_resources_dir;

void set_resources_dir(const std::string &dir)
{
    g_resources_dir = dir;
}

const std::string &resources_dir()
{
    return g_resources_dir;
}

static std::string g_local_dir;

void set_local_dir(const std::string &dir)
{
    g_local_dir = dir;
}

const std::string &localization_dir()
{
    return g_local_dir;
}

static std::string g_sys_shapes_dir;

void set_sys_shapes_dir(const std::string &dir)
{
    g_sys_shapes_dir = dir;
}

const std::string &sys_shapes_dir()
{
    return g_sys_shapes_dir;
}

static std::string g_custom_gcodes_dir;

void set_custom_gcodes_dir(const std::string &dir)
{
    g_custom_gcodes_dir = dir;
}

const std::string &custom_gcodes_dir()
{
    return g_custom_gcodes_dir;
}

static std::string g_data_dir;

void set_data_dir(const std::string &dir)
{
    g_data_dir = dir;
}

const std::string &data_dir()
{
    return g_data_dir;
}

#ifndef PREFLIGHT_PYTHON_VERSION_TAG
#define PREFLIGHT_PYTHON_VERSION_TAG "unknown"
#endif

std::string user_python_packages_dir()
{
    return (boost::filesystem::path(g_data_dir) / "python-packages" / PREFLIGHT_PYTHON_VERSION_TAG).string();
}

std::string custom_shapes_dir()
{
    return (boost::filesystem::path(g_data_dir) / "shapes").string();
}

static std::atomic<bool> debug_out_path_called(false);

std::string debug_out_path(const char *name, ...)
{
    static constexpr const char *PREFLIGHT_DEBUG_OUT_PATH_PREFIX = "out/";
    if (!debug_out_path_called.exchange(true))
    {
        std::string path = boost::filesystem::system_complete(PREFLIGHT_DEBUG_OUT_PATH_PREFIX).string();
        printf("Debugging output files will be written to %s\n", path.c_str());
    }
    char buffer[2048];
    va_list args;
    va_start(args, name);
    std::vsprintf(buffer, name, args);
    va_end(args);
    return std::string(PREFLIGHT_DEBUG_OUT_PATH_PREFIX) + std::string(buffer);
}

bool is_shapes_dir(const std::string &dir)
{
    return dir == sys_shapes_dir() || dir == custom_shapes_dir();
}

} // namespace Luminary

// GetDataDir is the one per-platform piece of the default data directory: Windows reads the roaming
// AppData folder, linux the XDG config directory, and on macOS MacUtils.mm supplies it.
#if defined(_WIN32)

#include <shlobj.h>

static std::string GetDataDir()
{
    HRESULT hr = E_FAIL;

    std::wstring buffer;
    buffer.resize(MAX_PATH);

    hr = ::SHGetFolderPathW(NULL, // parent window, not used
                            CSIDL_APPDATA,
                            NULL,               // access token (current user)
                            SHGFP_TYPE_CURRENT, // current path, not just default value
                            (LPWSTR) buffer.data());

    if (hr == E_FAIL)
    {
        // directory doesn't exist, maybe we can get its default value?
        hr = ::SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_DEFAULT, (LPWSTR) buffer.data());
    }

    for (int i = 0; i < MAX_PATH; i++)
        if (buffer.data()[i] == '\0')
        {
            buffer.resize(i);
            break;
        }

    return boost::nowide::narrow(buffer);
}

#elif defined(__linux__)

#include <stdlib.h>
#include <pwd.h>

std::optional<std::string> get_env(std::string_view key)
{
    const char *result{getenv(key.data())};
    if (result == nullptr)
    {
        return std::nullopt;
    }
    return std::string{result};
}

namespace
{
std::optional<boost::filesystem::path> get_home_dir(const std::string &subfolder)
{
    if (auto result{get_env("HOME")})
    {
        return *result + subfolder;
    }
    else
    {
        std::optional<std::string> user_name{get_env("USER")};
        if (!user_name)
        {
            user_name = get_env("LOGNAME");
        }
        struct passwd *who{user_name ? getpwnam(user_name->data()) : (struct passwd *) NULL};
        // make sure the user exists!
        if (!who)
        {
            who = getpwuid(getuid());
        }
        if (who)
        {
            return std::string{who->pw_dir} + subfolder;
        }
    }
    return std::nullopt;
}
} // namespace

namespace Luminary
{
std::optional<boost::filesystem::path> get_home_config_dir()
{
    return get_home_dir("/.config");
}

std::optional<boost::filesystem::path> get_home_local_dir()
{
    return get_home_dir("/.local");
}
} // namespace Luminary

std::string GetDataDir()
{
    if (auto result{get_env("XDG_CONFIG_HOME")})
    {
        return *result;
    }
    else if (auto result{Luminary::get_home_config_dir()})
    {
        return result->string();
    }

    BOOST_LOG_TRIVIAL(error) << "GetDataDir() > unsupported file layout";

    return {};
}

#endif

namespace Luminary
{

std::string get_default_datadir()
{
    const std::string config_dir = GetDataDir();
    std::string data_dir = (boost::filesystem::path(config_dir) / PREFLIGHT_APP_FULL_NAME).make_preferred().string();
    return data_dir;
}

} // namespace Luminary
