///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/

#pragma once

// CPU affinity helpers for working around multithreading instability on unstable or hybrid-topology CPUs
// (most commonly Intel 12th gen+ with P/E cores, or degraded Raptor Lake silicon).
// Implemented on Windows x86/x64 and Linux x86/x64. macOS and ARM builds get no-op stubs: the P/E core
// concept here is Intel hybrid specific, and macOS does not expose per-process P/E affinity by design.

#if (defined(_WIN32) || defined(__linux__)) && \
    (defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86))
#define PREFLIGHT_CPU_AFFINITY_SUPPORTED 1
#endif

namespace Luminary
{

// Returns true when the CPU reports more than one efficiency class (i.e. Intel hybrid P/E topology).
// Always returns false on non-Windows builds and on homogeneous CPUs (AMD Ryzen, older Intel, etc.).
bool has_hybrid_cpu_topology();

// Restrict the current process to P-cores only. No-op and returns false if the CPU is not hybrid
// or if we are not on Windows. Saves the previous affinity mask the first time it succeeds so
// restore_full_cpu_affinity() can undo it.
bool apply_pcore_only_affinity();

// Restore the process affinity mask captured before the first apply_pcore_only_affinity() call.
// No-op and returns false if nothing was saved or we are not on Windows.
bool restore_full_cpu_affinity();

} // namespace Luminary
