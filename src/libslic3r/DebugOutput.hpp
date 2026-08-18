///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#ifndef slic3r_DebugOutput_hpp_
#define slic3r_DebugOutput_hpp_

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <thread>

// Runtime debug output, selected per category on the command line via
// --debug <comma-list|all> (e.g. --debug serpentine,fill). The mask is set
// once at CLI parse before slicing and read-only thereafter, so a plain global
// is sufficient. Default 0 means every category is silent.
namespace Slic3r
{

// Adding a category? Update three places: this enum, debug_category_from_name()
// below, and the --debug tooltip + Run.cpp error strings that list valid names.
enum DebugCat : uint32_t
{
    DBG_FILL = 1u << 0,
    DBG_PERIMETERS = 1u << 1,
    DBG_INTERLOCK = 1u << 2,
    DBG_SERPENTINE = 1u << 3,
    DBG_BAOBAB = 1u << 4,
    // Support generation telemetry common to every generator (layer grid, contact anchoring),
    // as opposed to DBG_BAOBAB, which is the Baobab engine's own diagnostics.
    DBG_SUPPORT = 1u << 5,
    // Print stability analysis (SupportSpotsGenerator): per-line support points and the
    // aggregated issues behind the "Detected print stability issues" alert.
    DBG_STABILITY = 1u << 6,
    DBG_ALL = 0xFFFFFFFFu,
};

inline uint32_t g_debug_mask = 0;

inline bool debug_enabled(uint32_t cat)
{
    return (g_debug_mask & cat) != 0;
}

// Map a CLI category token to its bit; 0 if the name is not recognized.
inline uint32_t debug_category_from_name(std::string_view name)
{
    if (name == "fill")
        return DBG_FILL;
    if (name == "perimeters")
        return DBG_PERIMETERS;
    if (name == "interlock")
        return DBG_INTERLOCK;
    if (name == "serpentine")
        return DBG_SERPENTINE;
    if (name == "baobab")
        return DBG_BAOBAB;
    if (name == "support")
        return DBG_SUPPORT;
    if (name == "stability")
        return DBG_STABILITY;
    if (name == "all")
        return DBG_ALL;
    return 0;
}

// Background flusher. Flushing stdout on every line is a syscall per line; under
// heavy --debug that dominates. A write-triggered throttle does not help either:
// the final burst lands once slicing goes quiet, and with no later write nothing
// triggers a flush, so the tail stays buffered until exit. So a tiny thread
// flushes stdout on a fixed cadence while any --debug category is active - output
// lands promptly during AND after slicing, at a few flushes/sec regardless of
// line volume. Started by the CLI when --debug is parsed; the destructor stops
// the thread and flushes the tail at exit. fflush/fwrite are individually
// thread-safe (stdio locks the stream), so the flusher and writers coexist.
static constexpr int64_t DBG_FLUSH_INTERVAL_MS = 250;

class DbgFlusher
{
public:
    void start()
    {
        if (m_started.exchange(true))
            return; // start once
        m_thread = std::thread(
            [this]
            {
                while (!m_stop.load(std::memory_order_relaxed))
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(DBG_FLUSH_INTERVAL_MS));
                    std::fflush(stdout);
                }
            });
    }
    ~DbgFlusher()
    {
        m_stop.store(true, std::memory_order_relaxed);
        if (m_thread.joinable())
            m_thread.join();
        std::fflush(stdout);
    }

private:
    std::atomic<bool> m_started{false};
    std::atomic<bool> m_stop{false};
    std::thread m_thread;
};

inline DbgFlusher g_dbg_flusher;

// printf-style argument checking on GCC/Clang (no-op on MSVC).
#if defined(__GNUC__) || defined(__clang__)
#define SLIC3R_DBG_PRINTF_FMT __attribute__((format(printf, 4, 5)))
#else
#define SLIC3R_DBG_PRINTF_FMT
#endif

// The one debug-output function. Emits a single line
//     [<z-height>][<TYPE>] <payload>
// (newline appended automatically) when cat is enabled. Callers pass only the
// payload format - no prefix, no trailing newline. The whole line is assembled
// in one buffer and written with a single fwrite, so lines never interleave
// mid-content across TBB workers. This is the only place the line format lives;
// change it here to change every line.
SLIC3R_DBG_PRINTF_FMT inline void dbg_log(uint32_t cat, double z, const char *type, const char *fmt, ...)
{
    if (!debug_enabled(cat))
        return;
    va_list args;
    va_start(args, fmt);
    va_list measure;
    va_copy(measure, args);
    const int payload_len = vsnprintf(nullptr, 0, fmt, measure);
    va_end(measure);
    if (payload_len < 0)
    {
        va_end(args);
        return;
    }
    char prefix[48];
    int prefix_len = snprintf(prefix, sizeof(prefix), "[%.3f][%s] ", z, type);
    if (prefix_len < 0)
        prefix_len = 0;
    else if (prefix_len > int(sizeof(prefix)) - 1)
        prefix_len = int(sizeof(prefix)) - 1;
    std::string line;
    line.reserve(size_t(prefix_len) + size_t(payload_len) + 1);
    line.append(prefix, size_t(prefix_len));
    const size_t off = line.size();
    line.resize(off + size_t(payload_len) + 1);
    vsnprintf(&line[off], size_t(payload_len) + 1, fmt, args);
    va_end(args);
    line.resize(off + size_t(payload_len)); // drop the trailing NUL vsnprintf wrote
    line.push_back('\n');
    fwrite(line.data(), 1, line.size(), stdout);
    // No per-line flush: g_dbg_flusher pushes stdout on a fixed cadence. Started lazily here
    // as well as by the CLI parser, so whichever process actually emits lines is guaranteed a
    // live flusher next to its own stdout - GUI slicing sessions stream instead of buffering
    // until exit.
    g_dbg_flusher.start();
}

} // namespace Slic3r

#endif // slic3r_DebugOutput_hpp_
