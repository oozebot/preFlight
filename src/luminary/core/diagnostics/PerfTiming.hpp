///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include <chrono>
#include <cstdio>
#include <atomic>

namespace Luminary
{

// Set to true to enable pipeline stage timing output to stderr.
// When false, all timing code is eliminated by the compiler.
static constexpr bool PERF_TIMING = false;

// Wall-clock stage timer. Reports elapsed time since last reset/stage call.
class PerfStageTimer
{
public:
    void reset()
    {
        if constexpr (PERF_TIMING)
            m_t0 = std::chrono::steady_clock::now();
    }
    void stage(const char *name)
    {
        if constexpr (PERF_TIMING)
        {
            auto now = std::chrono::steady_clock::now();
            fprintf(stderr, "[TIMING] %-40s %7.1fms\n", name,
                    std::chrono::duration<double, std::milli>(now - m_t0).count());
            m_t0 = now;
        }
    }

private:
    std::chrono::steady_clock::time_point m_t0;
};

// Accumulator for timing across parallel threads. Use atomic counters
// that sum nanoseconds (sub-microsecond intervals must not truncate to zero
// when a probe wraps a short per-item call), then print a summary after the
// parallel region.
class PerfAccumTimer
{
public:
    void reset()
    {
        if constexpr (PERF_TIMING)
            m_ns.store(0, std::memory_order_relaxed);
    }
    void add(std::chrono::steady_clock::time_point start, std::chrono::steady_clock::time_point end)
    {
        if constexpr (PERF_TIMING)
            m_ns.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count(),
                           std::memory_order_relaxed);
    }
    double ms() const
    {
        if constexpr (PERF_TIMING)
            return m_ns.load(std::memory_order_relaxed) / 1e6;
        return 0.0;
    }

private:
    std::atomic<int64_t> m_ns{0};
};

// Event counter across parallel threads; compiled out with PERF_TIMING.
class PerfCounter
{
public:
    void add(int64_t n = 1)
    {
        if constexpr (PERF_TIMING)
            m_n.fetch_add(n, std::memory_order_relaxed);
    }
    int64_t get() const
    {
        if constexpr (PERF_TIMING)
            return m_n.load(std::memory_order_relaxed);
        return 0;
    }
    void reset()
    {
        if constexpr (PERF_TIMING)
            m_n.store(0, std::memory_order_relaxed);
    }

private:
    std::atomic<int64_t> m_n{0};
};

// Scoped timer that adds elapsed time to an accumulator on destruction.
class PerfScopedTimer
{
public:
    explicit PerfScopedTimer(PerfAccumTimer &accum) : m_accum(accum)
    {
        if constexpr (PERF_TIMING)
            m_start = std::chrono::steady_clock::now();
    }
    ~PerfScopedTimer()
    {
        if constexpr (PERF_TIMING)
            m_accum.add(m_start, std::chrono::steady_clock::now());
    }
    PerfScopedTimer(const PerfScopedTimer &) = delete;
    PerfScopedTimer &operator=(const PerfScopedTimer &) = delete;

private:
    PerfAccumTimer &m_accum;
    std::chrono::steady_clock::time_point m_start;
};

// Global slice-start timestamp for total elapsed measurement.
inline std::chrono::steady_clock::time_point &perf_slice_start()
{
    static std::chrono::steady_clock::time_point t;
    return t;
}

inline void perf_print(const char *fmt, ...)
{
    if constexpr (!PERF_TIMING)
        return;
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

} // namespace Luminary
