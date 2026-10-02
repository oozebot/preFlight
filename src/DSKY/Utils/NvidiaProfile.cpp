///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/

#include "NvidiaProfile.hpp"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <algorithm>
#include <cstring>

#include <boost/log/trivial.hpp>

#include "luminary/core/diagnostics/DebugCounters.hpp"

// We deliberately do not pull in the full NVAPI SDK. NVAPI exposes exactly one real export,
// nvapi_QueryInterface, and every other function is resolved at runtime by ID. The minimal
// type and struct layouts below are the subset we need to create a per-app profile and flip
// OGL_THREAD_CONTROL_ID. Sizes matter: NVAPI validates struct versions via sizeof, so these
// must match the driver's expected layouts exactly.

namespace Luminary
{
namespace
{

using NvU16 = unsigned short;
using NvU32 = unsigned int;
using NvS32 = int;

constexpr int NVAPI_UNICODE_STRING_MAX = 2048;
constexpr int NVAPI_BINARY_DATA_MAX = 4096;

using NvAPI_UnicodeString = NvU16[NVAPI_UNICODE_STRING_MAX];

constexpr NvU32 NVAPI_OK = 0;
// NvAPI_Status values that answer a query ("not there") rather than fail it
constexpr NvS32 NVAPI_SETTING_NOT_FOUND = -160;
constexpr NvS32 NVAPI_EXECUTABLE_NOT_FOUND = -166;

// Opaque handles; the driver treats these as pointers internally.
using NvDRSSessionHandle = void *;
using NvDRSProfileHandle = void *;

enum NVDRS_SETTING_TYPE
{
    NVDRS_DWORD_TYPE,
    NVDRS_BINARY_TYPE,
    NVDRS_STRING_TYPE,
    NVDRS_WSTRING_TYPE
};

enum NVDRS_SETTING_LOCATION
{
    NVDRS_CURRENT_PROFILE_LOCATION,
    NVDRS_GLOBAL_PROFILE_LOCATION,
    NVDRS_BASE_PROFILE_LOCATION,
    NVDRS_DEFAULT_PROFILE_LOCATION
};

struct NVDRS_BINARY_SETTING
{
    NvU32 valueLength;
    unsigned char valueData[NVAPI_BINARY_DATA_MAX];
};

// NVDRS_APPLICATION_V4: application entry within a profile.
struct NVDRS_APPLICATION_V4
{
    NvU32 version;
    NvU32 isPredefined;
    NvAPI_UnicodeString appName;
    NvAPI_UnicodeString userFriendlyName;
    NvAPI_UnicodeString launcher;
    NvAPI_UnicodeString fileInFolder;
    NvU32 isMetro : 1;
    NvU32 isCommandLine : 1;
    NvU32 reserved : 30;
    NvAPI_UnicodeString commandLine;
};

struct NVDRS_PROFILE_V1
{
    NvU32 version;
    NvAPI_UnicodeString profileName;
    NvU32 gpuSupport;
    NvU32 isPredefined;
    NvU32 numOfApps;
    NvU32 numOfSettings;
};

struct NVDRS_SETTING_V1
{
    NvU32 version;
    NvAPI_UnicodeString settingName;
    NvU32 settingId;
    NVDRS_SETTING_TYPE settingType;
    NVDRS_SETTING_LOCATION settingLocation;
    NvU32 isCurrentPredefined;
    NvU32 isPredefinedValid;
    union
    {
        NvU32 u32PredefinedValue;
        NVDRS_BINARY_SETTING binaryPredefinedValue;
        NvAPI_UnicodeString wszPredefinedValue;
    };
    union
    {
        NvU32 u32CurrentValue;
        NVDRS_BINARY_SETTING binaryCurrentValue;
        NvAPI_UnicodeString wszCurrentValue;
    };
};

// NVAPI encodes a version-size pair into the struct's version field.
constexpr NvU32 make_nvapi_version(std::size_t struct_size, NvU32 ver)
{
    return NvU32(struct_size) | (ver << 16);
}

#define NVDRS_APPLICATION_V4_VER make_nvapi_version(sizeof(NVDRS_APPLICATION_V4), 4)
#define NVDRS_PROFILE_V1_VER make_nvapi_version(sizeof(NVDRS_PROFILE_V1), 1)
#define NVDRS_SETTING_V1_VER make_nvapi_version(sizeof(NVDRS_SETTING_V1), 1)

// Function interface IDs resolved through nvapi_QueryInterface.
constexpr NvU32 ID_NvAPI_Initialize = 0x0150E828;
constexpr NvU32 ID_NvAPI_Unload = 0xD22BDD7E;
constexpr NvU32 ID_NvAPI_DRS_CreateSession = 0x0694D52E;
constexpr NvU32 ID_NvAPI_DRS_DestroySession = 0xDAD9CFF8;
constexpr NvU32 ID_NvAPI_DRS_LoadSettings = 0x375DBD6B;
constexpr NvU32 ID_NvAPI_DRS_SaveSettings = 0xFCBC7E14;
constexpr NvU32 ID_NvAPI_DRS_FindApplicationByName = 0xEEE566B2;
constexpr NvU32 ID_NvAPI_DRS_CreateProfile = 0xCC176068;
constexpr NvU32 ID_NvAPI_DRS_CreateApplication = 0x4347A9DE;
constexpr NvU32 ID_NvAPI_DRS_SetSetting = 0x577DD202;
constexpr NvU32 ID_NvAPI_DRS_GetSetting = 0x73BF8338;
constexpr NvU32 ID_NvAPI_DRS_DeleteProfileSetting = 0xE4A26362;

// OpenGL Threaded Optimization setting. DISABLE is the crash workaround; turning the workaround off
// deletes the setting from the profile, so the global setting applies again.
constexpr NvU32 OGL_THREAD_CONTROL_ID = 0x20C1221E;
constexpr NvU32 OGL_THREAD_CONTROL_DISABLE = 0x00000002;

// Function pointer signatures.
using FN_QueryInterface = void *(*) (NvU32);
using FN_Initialize = NvU32 (*)();
using FN_Unload = NvU32 (*)();
using FN_DRS_CreateSession = NvU32 (*)(NvDRSSessionHandle *);
using FN_DRS_DestroySession = NvU32 (*)(NvDRSSessionHandle);
using FN_DRS_LoadSettings = NvU32 (*)(NvDRSSessionHandle);
using FN_DRS_SaveSettings = NvU32 (*)(NvDRSSessionHandle);
using FN_DRS_FindApplicationByName = NvU32 (*)(NvDRSSessionHandle, NvU16 *, NvDRSProfileHandle *,
                                               NVDRS_APPLICATION_V4 *);
using FN_DRS_CreateProfile = NvU32 (*)(NvDRSSessionHandle, NVDRS_PROFILE_V1 *, NvDRSProfileHandle *);
using FN_DRS_CreateApplication = NvU32 (*)(NvDRSSessionHandle, NvDRSProfileHandle, NVDRS_APPLICATION_V4 *);
using FN_DRS_SetSetting = NvU32 (*)(NvDRSSessionHandle, NvDRSProfileHandle, NVDRS_SETTING_V1 *);
using FN_DRS_GetSetting = NvU32 (*)(NvDRSSessionHandle, NvDRSProfileHandle, NvU32, NVDRS_SETTING_V1 *);
using FN_DRS_DeleteProfileSetting = NvU32 (*)(NvDRSSessionHandle, NvDRSProfileHandle, NvU32);

// Copy an ASCII C string into a fixed NVAPI_UnicodeString buffer (UTF-16, null terminated).
void copy_ascii_to_unicode(NvAPI_UnicodeString dst, const char *src)
{
    std::memset(dst, 0, sizeof(NvAPI_UnicodeString));
    int i = 0;
    for (; src[i] != '\0' && i < NVAPI_UNICODE_STRING_MAX - 1; ++i)
        dst[i] = static_cast<NvU16>(static_cast<unsigned char>(src[i]));
    dst[i] = 0;
}

} // namespace

bool nvidia_driver_available()
{
    HMODULE nvapi = LoadLibraryW(L"nvapi64.dll");
    if (!nvapi)
        return false;
    auto query = reinterpret_cast<FN_QueryInterface>(GetProcAddress(nvapi, "nvapi_QueryInterface"));
    bool ok = false;
    if (query)
    {
        auto Init = reinterpret_cast<FN_Initialize>(query(ID_NvAPI_Initialize));
        auto Unload = reinterpret_cast<FN_Unload>(query(ID_NvAPI_Unload));
        if (Init && Unload && Init() == NVAPI_OK)
        {
            ok = true;
            Unload();
        }
    }
    FreeLibrary(nvapi);
    return ok;
}

namespace
{

// Every Windows executable preFlight ships runs the same OpenGL code. The first one decides the main
// profile; the others join it when the driver does not list them yet.
constexpr const char *PREFLIGHT_EXECUTABLES[] = {"preFlight.exe", "preFlight-console.exe", "preFlight-gcodeviewer.exe"};
constexpr std::size_t PREFLIGHT_EXECUTABLE_COUNT = sizeof(PREFLIGHT_EXECUTABLES) / sizeof(PREFLIGHT_EXECUTABLES[0]);

// Writes OGL_THREAD_CONTROL for every preFlight executable through a loaded nvapi64.dll, or deletes it when
// `disable` is false. Logs each failed step; returns false if any executable did not get the requested state.
bool write_thread_control(HMODULE nvapi, bool disable)
{
    auto query = reinterpret_cast<FN_QueryInterface>(GetProcAddress(nvapi, "nvapi_QueryInterface"));
    if (!query)
    {
        BOOST_LOG_TRIVIAL(warning) << "NVIDIA profile: nvapi64.dll does not export nvapi_QueryInterface";
        return false;
    }

    auto Init = reinterpret_cast<FN_Initialize>(query(ID_NvAPI_Initialize));
    auto Unload = reinterpret_cast<FN_Unload>(query(ID_NvAPI_Unload));
    auto Create = reinterpret_cast<FN_DRS_CreateSession>(query(ID_NvAPI_DRS_CreateSession));
    auto Destroy = reinterpret_cast<FN_DRS_DestroySession>(query(ID_NvAPI_DRS_DestroySession));
    auto Load = reinterpret_cast<FN_DRS_LoadSettings>(query(ID_NvAPI_DRS_LoadSettings));
    auto Save = reinterpret_cast<FN_DRS_SaveSettings>(query(ID_NvAPI_DRS_SaveSettings));
    auto FindApp = reinterpret_cast<FN_DRS_FindApplicationByName>(query(ID_NvAPI_DRS_FindApplicationByName));
    auto NewProf = reinterpret_cast<FN_DRS_CreateProfile>(query(ID_NvAPI_DRS_CreateProfile));
    auto NewApp = reinterpret_cast<FN_DRS_CreateApplication>(query(ID_NvAPI_DRS_CreateApplication));
    auto SetValue = reinterpret_cast<FN_DRS_SetSetting>(query(ID_NvAPI_DRS_SetSetting));
    auto DeleteValue = reinterpret_cast<FN_DRS_DeleteProfileSetting>(query(ID_NvAPI_DRS_DeleteProfileSetting));

    if (!Init || !Unload || !Create || !Destroy || !Load || !Save || !FindApp || !NewProf || !NewApp || !SetValue ||
        (!disable && !DeleteValue))
    {
        BOOST_LOG_TRIVIAL(warning) << "NVIDIA profile: nvapi64.dll lacks a required DRS entry point";
        return false;
    }

    if (const NvU32 status = Init(); status != NVAPI_OK)
    {
        BOOST_LOG_TRIVIAL(warning) << "NVIDIA profile: NvAPI_Initialize failed, status " << NvS32(status);
        return false;
    }

    // Applies the setting inside a loaded session.
    auto write_profiles = [&](NvDRSSessionHandle session) -> bool
    {
        bool complete = true;

        // The profile the driver lists the executable in, or null when it does not list it.
        auto find_profile = [&](const char *exe) -> NvDRSProfileHandle
        {
            NvAPI_UnicodeString name;
            copy_ascii_to_unicode(name, exe);
            NVDRS_APPLICATION_V4 app{};
            app.version = NVDRS_APPLICATION_V4_VER;
            NvDRSProfileHandle profile = nullptr;
            return FindApp(session, name, &profile, &app) == NVAPI_OK ? profile : nullptr;
        };

        // Adds the executable to `profile`. When the driver refuses because the executable is already
        // listed under a name the lookup missed, the profile that lists it is used instead.
        auto add_to_profile = [&](NvDRSProfileHandle profile, const char *exe) -> NvDRSProfileHandle
        {
            NVDRS_APPLICATION_V4 app{};
            app.version = NVDRS_APPLICATION_V4_VER;
            copy_ascii_to_unicode(app.appName, exe);
            copy_ascii_to_unicode(app.userFriendlyName, "preFlight");
            const NvU32 status = NewApp(session, profile, &app);
            if (status == NVAPI_OK)
                return profile;
            if (NvDRSProfileHandle listed = find_profile(exe))
                return listed;
            BOOST_LOG_TRIVIAL(warning) << "NVIDIA profile: DRS_CreateApplication failed for " << exe << ", status "
                                       << NvS32(status);
            return nullptr;
        };

        // The profile each executable carries the setting in; null when it carries none.
        NvDRSProfileHandle profiles[PREFLIGHT_EXECUTABLE_COUNT] = {};

        // Only create a new profile if preflight.exe is entirely unknown to NVIDIA. Attempting to
        // create a user profile that shadows an existing predefined match (e.g. NVIDIA's "Airport
        // Traffic Control 3" entry that happens to list preflight.exe) fails with
        // NVAPI_EXECUTABLE_ALREADY_IN_USE, which would leave our setting applied to no profile at
        // all. Writing to the predefined profile works, at the cost of a cosmetic
        // label mismatch in NVIDIA Control Panel. With `disable` false there is nothing to reset
        // for an unknown executable, so no profile is created.
        NvDRSProfileHandle main_profile = find_profile(PREFLIGHT_EXECUTABLES[0]);
        profiles[0] = main_profile;
        if (!main_profile && disable)
        {
            NVDRS_PROFILE_V1 new_profile{};
            new_profile.version = NVDRS_PROFILE_V1_VER;
            copy_ascii_to_unicode(new_profile.profileName, "preFlight");
            if (const NvU32 status = NewProf(session, &new_profile, &main_profile); status != NVAPI_OK)
            {
                BOOST_LOG_TRIVIAL(warning) << "NVIDIA profile: DRS_CreateProfile failed, status " << NvS32(status);
                main_profile = nullptr;
                complete = false;
            }
            else
            {
                profiles[0] = add_to_profile(main_profile, PREFLIGHT_EXECUTABLES[0]);
                if (!profiles[0])
                    complete = false;
            }
        }

        for (std::size_t i = 1; i < PREFLIGHT_EXECUTABLE_COUNT; ++i)
        {
            const char *exe = PREFLIGHT_EXECUTABLES[i];
            profiles[i] = find_profile(exe);
            if (profiles[i] || !disable)
                continue;
            if (!main_profile)
            {
                BOOST_LOG_TRIVIAL(warning) << "NVIDIA profile: no profile to add " << exe << " to";
                complete = false;
                continue;
            }
            profiles[i] = add_to_profile(main_profile, exe);
            if (!profiles[i])
                complete = false;
        }

        // A profile shared by several executables is written once.
        bool any_target = false;
        for (std::size_t i = 0; i < PREFLIGHT_EXECUTABLE_COUNT; ++i)
        {
            if (!profiles[i] || std::find(profiles, profiles + i, profiles[i]) != profiles + i)
                continue;
            if (disable)
            {
                NVDRS_SETTING_V1 setting{};
                setting.version = NVDRS_SETTING_V1_VER;
                setting.settingId = OGL_THREAD_CONTROL_ID;
                setting.settingType = NVDRS_DWORD_TYPE;
                setting.u32CurrentValue = OGL_THREAD_CONTROL_DISABLE;
                if (const NvU32 status = SetValue(session, profiles[i], &setting); status != NVAPI_OK)
                {
                    BOOST_LOG_TRIVIAL(warning) << "NVIDIA profile: DRS_SetSetting failed for the profile of "
                                               << PREFLIGHT_EXECUTABLES[i] << ", status " << NvS32(status);
                    complete = false;
                }
            }
            // A profile that does not set it already has the requested state
            else if (const NvU32 status = DeleteValue(session, profiles[i], OGL_THREAD_CONTROL_ID);
                     status != NVAPI_OK && NvS32(status) != NVAPI_SETTING_NOT_FOUND)
            {
                BOOST_LOG_TRIVIAL(warning) << "NVIDIA profile: DRS_DeleteProfileSetting failed for the profile of "
                                           << PREFLIGHT_EXECUTABLES[i] << ", status " << NvS32(status);
                complete = false;
            }
            any_target = true;
        }

        // Nothing to save when no executable carries a profile (a reset with nothing registered, or every add
        // failed). Otherwise saved even after a partial failure, so the executables that got the setting keep it.
        if (any_target)
        {
            if (const NvU32 status = Save(session); status != NVAPI_OK)
            {
                BOOST_LOG_TRIVIAL(warning) << "NVIDIA profile: DRS_SaveSettings failed, status " << NvS32(status);
                complete = false;
            }
        }
        return complete;
    };

    bool written = false;
    NvDRSSessionHandle session = nullptr;
    if (const NvU32 status = Create(&session); status != NVAPI_OK)
    {
        BOOST_LOG_TRIVIAL(warning) << "NVIDIA profile: DRS_CreateSession failed, status " << NvS32(status);
    }
    else
    {
        if (const NvU32 load_status = Load(session); load_status != NVAPI_OK)
            BOOST_LOG_TRIVIAL(warning) << "NVIDIA profile: DRS_LoadSettings failed, status " << NvS32(load_status);
        else
            written = write_profiles(session);
        Destroy(session);
    }

    Unload();
    return written;
}

// Reads OGL_THREAD_CONTROL from the profile of preFlight.exe through a loaded nvapi64.dll. Logs a failed step.
std::optional<bool> read_thread_control(HMODULE nvapi)
{
    auto query = reinterpret_cast<FN_QueryInterface>(GetProcAddress(nvapi, "nvapi_QueryInterface"));
    if (!query)
    {
        BOOST_LOG_TRIVIAL(warning) << "NVIDIA profile: nvapi64.dll does not export nvapi_QueryInterface";
        return std::nullopt;
    }

    auto Init = reinterpret_cast<FN_Initialize>(query(ID_NvAPI_Initialize));
    auto Unload = reinterpret_cast<FN_Unload>(query(ID_NvAPI_Unload));
    auto Create = reinterpret_cast<FN_DRS_CreateSession>(query(ID_NvAPI_DRS_CreateSession));
    auto Destroy = reinterpret_cast<FN_DRS_DestroySession>(query(ID_NvAPI_DRS_DestroySession));
    auto Load = reinterpret_cast<FN_DRS_LoadSettings>(query(ID_NvAPI_DRS_LoadSettings));
    auto FindApp = reinterpret_cast<FN_DRS_FindApplicationByName>(query(ID_NvAPI_DRS_FindApplicationByName));
    auto GetValue = reinterpret_cast<FN_DRS_GetSetting>(query(ID_NvAPI_DRS_GetSetting));
    if (!Init || !Unload || !Create || !Destroy || !Load || !FindApp || !GetValue)
    {
        BOOST_LOG_TRIVIAL(warning) << "NVIDIA profile: nvapi64.dll lacks a DRS entry point needed to read the profile";
        return std::nullopt;
    }

    if (const NvU32 status = Init(); status != NVAPI_OK)
    {
        BOOST_LOG_TRIVIAL(warning) << "NVIDIA profile: NvAPI_Initialize failed, status " << NvS32(status);
        return std::nullopt;
    }

    std::optional<bool> disabled;
    NvDRSSessionHandle session = nullptr;
    if (const NvU32 status = Create(&session); status != NVAPI_OK)
        BOOST_LOG_TRIVIAL(warning) << "NVIDIA profile: DRS_CreateSession failed, status " << NvS32(status);
    else
    {
        if (const NvU32 load_status = Load(session); load_status != NVAPI_OK)
            BOOST_LOG_TRIVIAL(warning) << "NVIDIA profile: DRS_LoadSettings failed, status " << NvS32(load_status);
        else
        {
            NvAPI_UnicodeString name;
            copy_ascii_to_unicode(name, PREFLIGHT_EXECUTABLES[0]);
            NVDRS_APPLICATION_V4 app{};
            app.version = NVDRS_APPLICATION_V4_VER;
            NvDRSProfileHandle profile = nullptr;
            const NvU32 find_status = FindApp(session, name, &profile, &app);
            if (find_status == NVAPI_OK)
            {
                NVDRS_SETTING_V1 setting{};
                setting.version = NVDRS_SETTING_V1_VER;
                const NvU32 get_status = GetValue(session, profile, OGL_THREAD_CONTROL_ID, &setting);
                // A value inherited from the global profile is not the profile's own
                if (get_status == NVAPI_OK)
                    disabled = setting.settingLocation == NVDRS_CURRENT_PROFILE_LOCATION &&
                               setting.settingType == NVDRS_DWORD_TYPE &&
                               setting.u32CurrentValue == OGL_THREAD_CONTROL_DISABLE;
                else if (NvS32(get_status) == NVAPI_SETTING_NOT_FOUND)
                    disabled = false;
                else
                    BOOST_LOG_TRIVIAL(warning) << "NVIDIA profile: DRS_GetSetting failed, status " << NvS32(get_status);
            }
            // The driver lists no profile for preFlight.exe, so none carries the setting
            else if (NvS32(find_status) == NVAPI_EXECUTABLE_NOT_FOUND)
                disabled = false;
            else
                BOOST_LOG_TRIVIAL(warning)
                    << "NVIDIA profile: DRS_FindApplicationByName failed, status " << NvS32(find_status);
        }
        Destroy(session);
    }

    Unload();
    return disabled;
}

} // namespace

bool set_nvidia_threaded_optimization(bool disable)
{
    // No NVIDIA driver: nothing to write, and not a failure, since the preference is only offered on NVIDIA systems.
    HMODULE nvapi = LoadLibraryW(L"nvapi64.dll");
    if (!nvapi)
    {
        BOOST_LOG_TRIVIAL(debug) << "NVIDIA profile: nvapi64.dll not found, nothing written";
        return false;
    }

    const bool complete = write_thread_control(nvapi, disable);
    FreeLibrary(nvapi);
    if (!complete)
        DBG_COUNT_LOAD("NVIDIA_PROFILE_WRITE_FAILED");
    return complete;
}

std::optional<bool> nvidia_threaded_optimization_disabled()
{
    // No NVIDIA driver: nothing to read, and not a failure
    HMODULE nvapi = LoadLibraryW(L"nvapi64.dll");
    if (!nvapi)
        return std::nullopt;

    const std::optional<bool> disabled = read_thread_control(nvapi);
    FreeLibrary(nvapi);
    if (!disabled)
        DBG_COUNT_LOAD("NVIDIA_PROFILE_READ_FAILED");
    return disabled;
}

} // namespace Luminary

#else // _WIN32

namespace Luminary
{

bool nvidia_driver_available()
{
    return false;
}
bool set_nvidia_threaded_optimization(bool)
{
    return false;
}
std::optional<bool> nvidia_threaded_optimization_disabled()
{
    return std::nullopt;
}

} // namespace Luminary

#endif // _WIN32
