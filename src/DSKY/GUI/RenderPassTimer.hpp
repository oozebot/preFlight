///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

// Per-pass frame timing for the capture runner, compiled into debug builds only (PREFLIGHT_TEST_HOOKS). Each mark
// finishes the GPU, so a marked region's time is its CPU submission plus its GPU work. Without the test hooks the
// RENDER_PASS_* macros compile to nothing.

#ifdef PREFLIGHT_TEST_HOOKS

#include <string>
#include <utility>
#include <vector>

namespace DSKY
{

class RenderPassTimer
{
public:
    // One frame's passes in the order they were first marked, with their times in milliseconds
    using Frame = std::vector<std::pair<std::string, double>>;

    static void set_enabled(bool enabled);
    static bool enabled();

    // Finishes the GPU, starts the clock and clears the frame's entries
    static void begin_frame();
    // Finishes the GPU and adds the time since the previous mark (or the frame's start) to the entry name;
    // a name marked twice in a frame accumulates
    static void mark(const char *name);
    // Appends the frame to the recorded frames
    static void end_frame();

    // The recorded frames, leaving none
    static std::vector<Frame> take_frames();
    static void clear();
};

} // namespace DSKY

#define RENDER_PASS_BEGIN_FRAME() ::DSKY::RenderPassTimer::begin_frame()
#define RENDER_PASS_MARK(name) ::DSKY::RenderPassTimer::mark(name)
#define RENDER_PASS_END_FRAME() ::DSKY::RenderPassTimer::end_frame()

#else

#define RENDER_PASS_BEGIN_FRAME() ((void) 0)
#define RENDER_PASS_MARK(name) ((void) 0)
#define RENDER_PASS_END_FRAME() ((void) 0)

#endif // PREFLIGHT_TEST_HOOKS
