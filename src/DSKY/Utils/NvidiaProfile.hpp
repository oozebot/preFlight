///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/

#pragma once

#include <optional>

// NVIDIA driver profile helper. Creates (or updates) a per-application driver profile for preFlight.exe,
// preFlight-console.exe and preFlight-gcodeviewer.exe with OpenGL "Threaded Optimization" turned off. All
// three load the same renderer. This is the other common cause of preFlight slicing
// crashes on Windows (NVIDIA's driver-side multithreaded GL optimization is known to race with slicer
// worker threads). Implemented on Windows only; a no-op stub on other platforms.
//
// Takes effect on the NEXT preFlight launch (the driver caches profile state at GL init time). Idempotent:
// calling repeatedly does nothing if the setting is already in place.

namespace Luminary
{

// Returns true on NVIDIA systems where we can talk to the driver (nvapi64.dll present and
// initializable). Used to decide whether to show the UI toggle at all.
bool nvidia_driver_available();

// Write the OGL_THREAD_CONTROL setting on the profile of each preFlight executable (preFlight.exe,
// preFlight-console.exe, preFlight-gcodeviewer.exe). `disable` = true sets it to DISABLE (the crash
// workaround), adding any executable the driver does not list yet; `disable` = false removes the setting from
// the profiles of the executables the driver lists, so the global setting (or the profile's predefined value)
// applies again. Returns true when every executable has the requested state; a failed step is logged and
// counted as NVIDIA_PROFILE_WRITE_FAILED. Returns false without writing on non-Windows or non-NVIDIA systems.
bool set_nvidia_threaded_optimization(bool disable);

// Whether the driver profile of preFlight.exe carries OGL_THREAD_CONTROL = DISABLE: false when the driver lists
// no profile for it or the profile does not set it. nullopt when NVAPI is unavailable or a query fails (counted
// as NVIDIA_PROFILE_READ_FAILED).
std::optional<bool> nvidia_threaded_optimization_disabled();

// Profile layout the writer produces, stored in AppConfig ("cpu_nvidia_profile_revision") after a successful
// enable. A build that changes the layout bumps it, so startup rewrites the profile once for users who
// enabled the preference earlier.
inline constexpr const char *NVIDIA_PROFILE_REVISION = "2";

} // namespace Luminary
