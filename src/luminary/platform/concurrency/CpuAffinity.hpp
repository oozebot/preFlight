///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/

#pragma once

#include <cstddef>

// CPU affinity helpers for working around multithreading instability on unstable or hybrid-topology CPUs
// (CPUs with performance and efficient cores, for example Intel 12th gen and later, or degraded Raptor Lake
// silicon). Implemented on Windows x86/x64 (any vendor, from the processors' efficiency classes) and Linux
// x86/x64 (Intel only: the kernel lists the core types in the Intel perf PMU nodes). macOS and ARM builds get
// no-op stubs: macOS does not expose per-process P/E affinity by design.

#if (defined(_WIN32) || defined(__linux__)) && \
    (defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86))
#define PREFLIGHT_CPU_AFFINITY_SUPPORTED 1
#endif

namespace Luminary
{

// Returns true when the CPU has performance and efficient cores. Always false on homogeneous CPUs and on the
// platforms without an implementation (see above).
bool has_hybrid_cpu_topology();

// Restrict the current process to P-cores only. No-op and returns false if the CPU is not hybrid or the
// platform has no implementation. Saves the previous affinity mask the first time it succeeds so
// restore_full_cpu_affinity() can undo it.
bool apply_pcore_only_affinity();

// Restore the process affinity mask captured before the first apply_pcore_only_affinity() call.
// No-op and returns false if nothing was saved.
bool restore_full_cpu_affinity();

// The logical processors apply_pcore_only_affinity() keeps, 0 on a CPU without hybrid cores
std::size_t pcore_logical_count();

// The slicing thread cap (0 = all threads) and the P-core preference applied together: with P-cores only the
// cap is also at most the P-core count, since the worker pool keeps the size it started with. Returns the cap
// applied (0 = none). Safe to call again at any time.
std::size_t apply_cpu_policy(std::size_t user_cap, bool pcores_only);

// The logical processors the process may run on (its affinity mask), for the debug header
std::size_t process_affinity_popcount();

} // namespace Luminary
