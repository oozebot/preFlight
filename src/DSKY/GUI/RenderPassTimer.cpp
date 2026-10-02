///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "RenderPassTimer.hpp"

#ifdef PREFLIGHT_TEST_HOOKS

#include "3DScene.hpp"

#include <glad/gl.h>

#include <chrono>
#include <cstring>

namespace DSKY
{
namespace
{
bool s_enabled = false;
bool s_in_frame = false;
std::chrono::steady_clock::time_point s_last;
RenderPassTimer::Frame s_frame;
std::vector<RenderPassTimer::Frame> s_frames;
} // namespace

void RenderPassTimer::set_enabled(bool enabled)
{
    s_enabled = enabled;
    s_in_frame = false;
    s_frame.clear();
}

bool RenderPassTimer::enabled()
{
    return s_enabled;
}

void RenderPassTimer::begin_frame()
{
    if (!s_enabled)
        return;
    glsafe(::glFinish());
    s_frame.clear();
    s_in_frame = true;
    s_last = std::chrono::steady_clock::now();
}

void RenderPassTimer::mark(const char *name)
{
    if (!s_enabled || !s_in_frame)
        return;
    glsafe(::glFinish());
    const auto now = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(now - s_last).count();
    auto it = s_frame.begin();
    while (it != s_frame.end() && std::strcmp(it->first.c_str(), name) != 0)
        ++it;
    if (it != s_frame.end())
        it->second += ms;
    else
        s_frame.emplace_back(name, ms);
    // The bookkeeping above stays out of the next pass's time
    s_last = std::chrono::steady_clock::now();
}

void RenderPassTimer::end_frame()
{
    if (!s_enabled || !s_in_frame)
        return;
    s_frames.emplace_back(std::move(s_frame));
    s_frame.clear();
    s_in_frame = false;
}

std::vector<RenderPassTimer::Frame> RenderPassTimer::take_frames()
{
    std::vector<Frame> frames = std::move(s_frames);
    s_frames.clear();
    return frames;
}

void RenderPassTimer::clear()
{
    s_frames.clear();
    s_frame.clear();
    s_in_frame = false;
}

} // namespace DSKY

#endif // PREFLIGHT_TEST_HOOKS
