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
#include <cstring>

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

// OpenGL Threaded Optimization setting. DISABLE is the crash workaround; DEFAULT hands control
// back to the driver (letting the global / per-GPU default apply).
constexpr NvU32 OGL_THREAD_CONTROL_ID = 0x20C1221E;
constexpr NvU32 OGL_THREAD_CONTROL_DISABLE = 0x00000002;
constexpr NvU32 OGL_THREAD_CONTROL_DEFAULT = 0x00000000;

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

bool set_nvidia_threaded_optimization(bool disable)
{
    HMODULE nvapi = LoadLibraryW(L"nvapi64.dll");
    if (!nvapi)
        return false;

    auto query = reinterpret_cast<FN_QueryInterface>(GetProcAddress(nvapi, "nvapi_QueryInterface"));
    if (!query)
    {
        FreeLibrary(nvapi);
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

    if (!Init || !Unload || !Create || !Destroy || !Load || !Save || !FindApp || !NewProf || !NewApp || !SetValue)
    {
        FreeLibrary(nvapi);
        return false;
    }

    if (Init() != NVAPI_OK)
    {
        FreeLibrary(nvapi);
        return false;
    }

    bool success = false;
    NvDRSSessionHandle session = nullptr;
    if (Create(&session) == NVAPI_OK && Load(session) == NVAPI_OK)
    {
        NVDRS_APPLICATION_V4 app_lookup{};
        app_lookup.version = NVDRS_APPLICATION_V4_VER;
        copy_ascii_to_unicode(app_lookup.appName, "preFlight.exe");

        NvDRSProfileHandle profile = nullptr;
        NvU32 find_status = FindApp(session, app_lookup.appName, &profile, &app_lookup);

        // Only create a new profile if preflight.exe is entirely unknown to NVIDIA. Attempting to
        // create a user profile that shadows an existing predefined match (e.g. NVIDIA's "Airport
        // Traffic Control 3" entry that happens to list preflight.exe) fails with
        // NVAPI_EXECUTABLE_ALREADY_IN_USE, which would leave our setting applied to no profile at
        // all. Writing to the predefined profile works, at the cost of a cosmetic
        // label mismatch in NVIDIA Control Panel.
        if (find_status != NVAPI_OK)
        {
            NVDRS_PROFILE_V1 new_profile{};
            new_profile.version = NVDRS_PROFILE_V1_VER;
            copy_ascii_to_unicode(new_profile.profileName, "preFlight");

            if (NewProf(session, &new_profile, &profile) == NVAPI_OK)
            {
                NVDRS_APPLICATION_V4 new_app{};
                new_app.version = NVDRS_APPLICATION_V4_VER;
                copy_ascii_to_unicode(new_app.appName, "preFlight.exe");
                copy_ascii_to_unicode(new_app.userFriendlyName, "preFlight");
                (void) NewApp(session, profile, &new_app);
            }
            else
            {
                profile = nullptr;
            }
        }

        if (profile)
        {
            NVDRS_SETTING_V1 setting{};
            setting.version = NVDRS_SETTING_V1_VER;
            setting.settingId = OGL_THREAD_CONTROL_ID;
            setting.settingType = NVDRS_DWORD_TYPE;
            setting.u32CurrentValue = disable ? OGL_THREAD_CONTROL_DISABLE : OGL_THREAD_CONTROL_DEFAULT;

            if (SetValue(session, profile, &setting) == NVAPI_OK && Save(session) == NVAPI_OK)
                success = true;
        }

        Destroy(session);
    }

    Unload();
    FreeLibrary(nvapi);
    return success;
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

} // namespace Luminary

#endif // _WIN32
