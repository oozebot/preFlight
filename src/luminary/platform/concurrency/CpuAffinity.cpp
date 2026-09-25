///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/

#include "CpuAffinity.hpp"

#if defined(PREFLIGHT_CPU_AFFINITY_SUPPORTED) && defined(_WIN32)

// Bump Windows target for this translation unit only: GetSystemCpuSetInformation and the
// SYSTEM_CPU_SET_INFORMATION struct are declared starting with the Windows 10 SDK (_WIN32_WINNT >= 0x0A00).
// The rest of preFlight targets 0x0602 (Windows 8), so we raise it locally here.
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0A00
#ifdef WINVER
#undef WINVER
#endif
#define WINVER 0x0A00

#include <windows.h>
#include <vector>

namespace Luminary
{

namespace
{
// Pull the full SYSTEM_CPU_SET_INFORMATION buffer for the current process.
bool query_cpu_sets(std::vector<BYTE> &buffer_out)
{
    ULONG size = 0;
    HANDLE proc = GetCurrentProcess();
    GetSystemCpuSetInformation(nullptr, 0, &size, proc, 0);
    if (size == 0)
        return false;
    buffer_out.resize(size);
    if (!GetSystemCpuSetInformation(reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buffer_out.data()), size, &size, proc,
                                    0))
        return false;
    return true;
}

// Walk the variable-length SYSTEM_CPU_SET_INFORMATION records and invoke fn(record) for each CpuSet entry.
template<typename Fn>
void for_each_cpu_set(const std::vector<BYTE> &buffer, Fn fn)
{
    ULONG offset = 0;
    while (offset < buffer.size())
    {
        auto *info = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION *>(buffer.data() + offset);
        if (info->Size == 0)
            break;
        if (info->Type == CpuSetInformation)
            fn(*info);
        offset += info->Size;
    }
}

// Saved mask captured on first successful apply so restore can put it back. Zero means "not captured yet".
DWORD_PTR g_saved_process_mask = 0;
DWORD_PTR g_saved_system_mask = 0;
} // namespace

bool has_hybrid_cpu_topology()
{
    std::vector<BYTE> buffer;
    if (!query_cpu_sets(buffer))
        return false;

    bool have_any = false;
    BYTE min_class = 0xFF;
    BYTE max_class = 0;
    for_each_cpu_set(buffer,
                     [&](const SYSTEM_CPU_SET_INFORMATION &info)
                     {
                         BYTE ec = info.CpuSet.EfficiencyClass;
                         if (!have_any)
                         {
                             min_class = max_class = ec;
                             have_any = true;
                         }
                         else
                         {
                             if (ec < min_class)
                                 min_class = ec;
                             if (ec > max_class)
                                 max_class = ec;
                         }
                     });

    return have_any && (min_class != max_class);
}

bool apply_pcore_only_affinity()
{
    std::vector<BYTE> buffer;
    if (!query_cpu_sets(buffer))
        return false;

    // Identify the highest EfficiencyClass (P-cores) and build a mask of their logical processors.
    BYTE max_class = 0;
    for_each_cpu_set(buffer,
                     [&](const SYSTEM_CPU_SET_INFORMATION &info)
                     {
                         if (info.CpuSet.EfficiencyClass > max_class)
                             max_class = info.CpuSet.EfficiencyClass;
                     });

    DWORD_PTR pcore_mask = 0;
    bool homogeneous = true;
    for_each_cpu_set(buffer,
                     [&](const SYSTEM_CPU_SET_INFORMATION &info)
                     {
                         if (info.CpuSet.EfficiencyClass != max_class)
                             homogeneous = false;
                         else if (info.CpuSet.LogicalProcessorIndex < sizeof(DWORD_PTR) * 8)
                             pcore_mask |= (DWORD_PTR(1) << info.CpuSet.LogicalProcessorIndex);
                     });

    if (homogeneous || pcore_mask == 0)
        return false;

    if (g_saved_process_mask == 0)
    {
        DWORD_PTR process_mask = 0;
        DWORD_PTR system_mask = 0;
        if (GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask))
        {
            g_saved_process_mask = process_mask;
            g_saved_system_mask = system_mask;
        }
    }

    return SetProcessAffinityMask(GetCurrentProcess(), pcore_mask) != 0;
}

bool restore_full_cpu_affinity()
{
    if (g_saved_process_mask == 0)
        return false;
    BOOL ok = SetProcessAffinityMask(GetCurrentProcess(), g_saved_process_mask);
    g_saved_process_mask = 0;
    g_saved_system_mask = 0;
    return ok != 0;
}

} // namespace Luminary

#elif defined(PREFLIGHT_CPU_AFFINITY_SUPPORTED) && defined(__linux__)

// Linux x86/x64 path. Intel hybrid CPUs on kernels 5.18+ expose per-type sysfs groups:
// /sys/devices/cpu_core/cpus lists Performance-core CPUs, /sys/devices/cpu_atom/cpus lists Efficient-cores.
// Both files use the standard cpulist format (comma-separated ranges, e.g. "0-15" or "0,2,4-7").
// Older kernels or non-hybrid CPUs are detected by the absence/emptiness of these files.
//
// Unlike Windows' SetProcessAffinityMask which retargets every thread atomically, Linux
// sched_setaffinity only affects the target thread. To make live preference toggles take effect
// without a restart, we iterate /proc/self/task/ and apply the mask to every thread in the process.

#include <sched.h>
#include <dirent.h>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace Luminary
{

namespace
{
bool parse_cpulist(const std::string &line, std::vector<int> &out)
{
    out.clear();
    std::stringstream ss(line);
    std::string token;
    while (std::getline(ss, token, ','))
    {
        if (token.empty())
            continue;
        auto dash = token.find('-');
        try
        {
            if (dash == std::string::npos)
            {
                out.push_back(std::stoi(token));
            }
            else
            {
                int lo = std::stoi(token.substr(0, dash));
                int hi = std::stoi(token.substr(dash + 1));
                for (int i = lo; i <= hi; ++i)
                    out.push_back(i);
            }
        }
        catch (...)
        {
            return false;
        }
    }
    return !out.empty();
}

bool read_cpulist_file(const char *path, std::vector<int> &out)
{
    std::ifstream f(path);
    if (!f.is_open())
        return false;
    std::string line;
    std::getline(f, line);
    return parse_cpulist(line, out);
}

cpu_set_t g_saved_affinity;
bool g_saved_affinity_valid = false;

// Apply a mask to every thread in the current process by walking /proc/self/task/.
// Returns true if at least one thread was updated successfully. Individual thread failures
// (kernel helper threads, threads exiting mid-iteration, permission quirks) are ignored.
bool apply_mask_to_all_threads(const cpu_set_t &mask)
{
    DIR *d = opendir("/proc/self/task");
    if (!d)
        return sched_setaffinity(0, sizeof(mask), &mask) == 0;

    bool any_ok = false;
    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr)
    {
        if (ent->d_name[0] < '0' || ent->d_name[0] > '9')
            continue;
        char *end = nullptr;
        long tid = std::strtol(ent->d_name, &end, 10);
        if (end == ent->d_name || (end != nullptr && *end != '\0'))
            continue;
        if (sched_setaffinity(static_cast<pid_t>(tid), sizeof(mask), &mask) == 0)
            any_ok = true;
    }
    closedir(d);
    return any_ok;
}
} // namespace

bool has_hybrid_cpu_topology()
{
    std::vector<int> p, e;
    return read_cpulist_file("/sys/devices/cpu_core/cpus", p) && read_cpulist_file("/sys/devices/cpu_atom/cpus", e) &&
           !p.empty() && !e.empty();
}

bool apply_pcore_only_affinity()
{
    std::vector<int> p, e;
    if (!read_cpulist_file("/sys/devices/cpu_core/cpus", p) || p.empty() ||
        !read_cpulist_file("/sys/devices/cpu_atom/cpus", e) || e.empty())
        return false;

    if (!g_saved_affinity_valid)
    {
        if (sched_getaffinity(0, sizeof(g_saved_affinity), &g_saved_affinity) == 0)
            g_saved_affinity_valid = true;
    }

    cpu_set_t mask;
    CPU_ZERO(&mask);
    for (int cpu : p)
    {
        if (cpu >= 0 && cpu < CPU_SETSIZE)
            CPU_SET(cpu, &mask);
    }
    return apply_mask_to_all_threads(mask);
}

bool restore_full_cpu_affinity()
{
    if (!g_saved_affinity_valid)
        return false;
    bool ok = apply_mask_to_all_threads(g_saved_affinity);
    g_saved_affinity_valid = false;
    return ok;
}

} // namespace Luminary

#else // PREFLIGHT_CPU_AFFINITY_SUPPORTED

namespace Luminary
{

bool has_hybrid_cpu_topology()
{
    return false;
}
bool apply_pcore_only_affinity()
{
    return false;
}
bool restore_full_cpu_affinity()
{
    return false;
}

} // namespace Luminary

#endif // PREFLIGHT_CPU_AFFINITY_SUPPORTED
