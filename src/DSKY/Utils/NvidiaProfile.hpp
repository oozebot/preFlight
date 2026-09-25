///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/

#pragma once

// NVIDIA driver profile helper. Creates (or updates) a per-application driver profile for preFlight.exe
// with OpenGL "Threaded Optimization" turned off. This is the other common cause of preFlight slicing
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

// Write the OGL_THREAD_CONTROL setting on the preFlight profile. `disable` = true sets it to
// DISABLE (the crash workaround); `disable` = false sets it back to the driver default (0).
// Returns true on success. No-op on non-Windows or non-NVIDIA systems.
bool set_nvidia_threaded_optimization(bool disable);

} // namespace Luminary
