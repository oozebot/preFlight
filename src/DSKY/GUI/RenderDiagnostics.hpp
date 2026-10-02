///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include <cstddef>
#include <string>

namespace DSKY
{

// What a canvas's last frame was rendered with, next to what the preferences ask for. Filled by
// GLCanvas3D::render every frame; read by System Info, the render statistics window, the Preferences
// "in effect" lines and the test hooks.
struct RenderFrameInfo
{
    bool valid{false};
    int canvas_width{0};
    int canvas_height{0};
    // Camera viewport in window pixels; wider than the canvas when the reserved strip is hidden
    int viewport[4]{0, 0, 0, 0};
    bool strip_hidden{false};
    // MSAA: the preference (-1 Auto, 0 off, else samples), the window framebuffer's samples, and the samples
    // of the framebuffer the 3D scene was drawn into
    int msaa_requested{-1};
    int msaa_window{0};
    int msaa_scene{0};
    // SSAA: requested and effective render scale, and the size the scene was rendered at
    double ssaa_requested{1.0};
    double ssaa_effective{1.0};
    std::string ssaa_reason;
    int scene_width{0};
    int scene_height{0};
    // Lighting: the preference, the tier the frame used, and why it is lower when it is
    std::string lighting_requested;
    std::string lighting_effective;
    std::string lighting_reason;
    // Toolpath prefilter: the preference, whether the frame's toolpath draw used it, and why it did not
    bool prefilter_requested{false};
    bool prefilter_active{false};
    std::string prefilter_reason{"no toolpaths drawn"};
    // SSAO normal of toolpath pixels: the depth step in G-buffer texels, 0 when they keep the G-buffer normal
    float ao_toolpath_normals{0.0f};
    // Offscreen targets the canvas holds (scene target and lighting passes), in bytes
    size_t offscreen_bytes{0};
};

namespace RenderDiagnostics
{

// Stores the last frame of the canvas named `role` ("Prepare", "Preview")
void record_frame(const std::string &role, const RenderFrameInfo &info);

// The last frame of `role`, or nullptr before its first frame
const RenderFrameInfo *last_frame(const std::string &role);

// The render state block of System Info: every canvas's last frame and every render fallback counter
std::string to_string(bool for_github);

// One frame as "key=value" lines, for the test hooks' capture sidecars
std::string to_key_values(const RenderFrameInfo &info);

// The value of every render fallback counter (the RENDER_ rows of the debug counters) as "NAME=value" lines
std::string counters_text();

// A reason that lifts by itself (a clipping plane, an open painting tool, an empty scene, the multiple bed
// overview, a tiny viewport), or no reason at all: the frame drew less than requested for now, nothing failed
bool is_temporary_reason(const std::string &reason);

// True when each heavy feature the frame requested (Full lighting, supersampling) ran, or was refused for a
// lasting reason (a failure or a missing capability), so the frame exercised the requested settings
bool requested_features_settled(const RenderFrameInfo &info);

#ifdef PREFLIGHT_TEST_HOOKS
// PREFLIGHT_RENDER_FAIL lists the render failures a test forces, comma separated: "scene", "gbuffer", "shader".
// Read once per session.
bool render_fail_forced(const std::string &what);
#endif

} // namespace RenderDiagnostics
} // namespace DSKY
