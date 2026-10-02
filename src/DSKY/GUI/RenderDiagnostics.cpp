///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "RenderDiagnostics.hpp"

#include "GUI.hpp"
#include "luminary/core/diagnostics/DebugCounters.hpp"
#include "luminary/platform/concurrency/CpuAffinity.hpp"
#include "luminary/platform/concurrency/WorkerPolicy.hpp"
#include "luminary/presets/app_config/AppConfig.hpp"

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <sstream>

namespace DSKY
{
namespace RenderDiagnostics
{
namespace
{

std::map<std::string, RenderFrameInfo> &frames()
{
    static std::map<std::string, RenderFrameInfo> s_frames;
    return s_frames;
}

std::string msaa_text(int samples)
{
    if (samples < 0)
        return "Auto";
    if (samples <= 1)
        return "Off";
    return std::to_string(samples) + "x";
}

std::string scale_text(double scale)
{
    if (scale <= 1.0)
        return "Off";
    std::ostringstream out;
    out << std::setprecision(4) << scale << "x";
    return out.str();
}

} // namespace

void record_frame(const std::string &role, const RenderFrameInfo &info)
{
    RenderFrameInfo &slot = frames()[role];
    slot = info;
    slot.valid = true;
}

const RenderFrameInfo *last_frame(const std::string &role)
{
    auto it = frames().find(role);
    return it != frames().end() && it->second.valid ? &it->second : nullptr;
}

std::string counters_text()
{
    std::ostringstream out;
    for (const auto &[name, value] : Luminary::DbgCounters::inst().values())
        if (name.rfind("RENDER_", 0) == 0)
            out << name << "=" << value << "\n";
    return out.str();
}

bool is_temporary_reason(const std::string &reason)
{
    static const char *const temporary[] = {"clipping plane active", "painting tool open", "multiple bed overview",
                                            "nothing to shade", "viewport too small"};
    return reason.empty() || std::any_of(std::begin(temporary), std::end(temporary),
                                         [&reason](const char *block) { return reason == block; });
}

bool requested_features_settled(const RenderFrameInfo &info)
{
    const bool lighting = info.lighting_requested != "full" || info.lighting_effective == "Full" ||
                          !is_temporary_reason(info.lighting_reason);
    // A scale capped by the GPU's limits still ran supersampled
    const bool ssaa = info.ssaa_requested <= 1.0 || info.ssaa_effective > 1.0 || !is_temporary_reason(info.ssaa_reason);
    return lighting && ssaa;
}

#ifdef PREFLIGHT_TEST_HOOKS
bool render_fail_forced(const std::string &what)
{
    static const std::string value = []()
    {
        const char *env = std::getenv("PREFLIGHT_RENDER_FAIL");
        return env != nullptr ? std::string(env) : std::string();
    }();
    size_t start = 0;
    while (start <= value.size())
    {
        const size_t end = std::min(value.find(',', start), value.size());
        if (value.compare(start, end - start, what) == 0)
            return true;
        start = end + 1;
    }
    return false;
}
#endif

std::string to_key_values(const RenderFrameInfo &info)
{
    std::ostringstream out;
    out << "canvas=" << info.canvas_width << "x" << info.canvas_height << "\n";
    out << "viewport=" << info.viewport[0] << "," << info.viewport[1] << "," << info.viewport[2] << ","
        << info.viewport[3] << "\n";
    out << "strip_hidden=" << (info.strip_hidden ? 1 : 0) << "\n";
    out << "msaa_requested=" << info.msaa_requested << "\n";
    out << "msaa_window=" << info.msaa_window << "\n";
    out << "msaa_scene=" << info.msaa_scene << "\n";
    out << "ssaa_requested=" << info.ssaa_requested << "\n";
    out << "ssaa_effective=" << info.ssaa_effective << "\n";
    out << "ssaa_reason=" << info.ssaa_reason << "\n";
    out << "scene=" << info.scene_width << "x" << info.scene_height << "\n";
    out << "lighting_requested=" << info.lighting_requested << "\n";
    out << "lighting_effective=" << info.lighting_effective << "\n";
    out << "lighting_reason=" << info.lighting_reason << "\n";
    out << "prefilter_requested=" << (info.prefilter_requested ? 1 : 0) << "\n";
    out << "prefilter_active=" << (info.prefilter_active ? 1 : 0) << "\n";
    out << "prefilter_reason=" << info.prefilter_reason << "\n";
    out << "ao_toolpath_normals=" << info.ao_toolpath_normals << "\n";
    out << "offscreen_bytes=" << info.offscreen_bytes << "\n";
    return out.str();
}

std::string to_string(bool for_github)
{
    const bool html = !for_github;
    const std::string b_start = html ? "<b>" : "";
    const std::string b_end = html ? "</b>" : "";
    const std::string line_end = html ? "<br>" : "\n";

    std::ostringstream out;
    out << b_start << "Rendering" << b_end << line_end;
    bool any = false;
    for (const auto &[role, info] : frames())
    {
        if (!info.valid)
            continue;
        any = true;
        out << b_start << role << ":" << b_end << line_end;
        out << "  MSAA: " << msaa_text(info.msaa_requested) << " requested, window " << msaa_text(info.msaa_window)
            << ", scene " << msaa_text(info.msaa_scene) << line_end;
        out << "  SSAA: " << scale_text(info.ssaa_requested) << " requested, " << scale_text(info.ssaa_effective)
            << " in effect";
        if (!info.ssaa_reason.empty())
            out << " (" << info.ssaa_reason << ")";
        out << ", scene " << info.scene_width << "x" << info.scene_height << line_end;
        out << "  Lighting: " << info.lighting_requested << " requested, " << info.lighting_effective << " in effect";
        if (!info.lighting_reason.empty())
            out << " (" << info.lighting_reason << ")";
        out << line_end;
        out << "  Toolpath prefilter: " << (info.prefilter_active ? "on" : "off");
        if (!info.prefilter_active && !info.prefilter_reason.empty())
            out << " (" << info.prefilter_reason << ")";
        // The SSAO normal of toolpath pixels matters only where the Full tier's passes ran
        if (info.lighting_effective == "Full")
        {
            if (info.ao_toolpath_normals > 0.0f)
                out << ", AO toolpath normals from depth at " << info.ao_toolpath_normals << " texels";
            else
                out << ", AO toolpath normals from the G-buffer";
        }
        out << line_end;
        out << "  Viewport: " << info.viewport[0] << "," << info.viewport[1] << " " << info.viewport[2] << "x"
            << info.viewport[3] << " in a " << info.canvas_width << "x" << info.canvas_height << " canvas"
            << (info.strip_hidden ? ", strip hidden" : "") << line_end;
        out << "  Offscreen targets: " << (info.offscreen_bytes + (1 << 19)) / (1 << 20) << " MiB" << line_end;
    }
    if (!any)
        out << "No frame rendered yet" << line_end;
    // The slicing thread policy in effect
    out << b_start << "CPU:" << b_end << line_end;
    out << "  Slicing threads: " << Luminary::tbb_parallelism() << line_end;
    out << "  Processors available: " << Luminary::process_affinity_popcount() << line_end;
    if (const std::size_t pcores = Luminary::pcore_logical_count(); pcores > 0)
    {
        const Luminary::AppConfig *config = get_app_config();
        out << "  Performance cores: " << pcores
            << (config != nullptr && config->get_bool("cpu_pcores_only") ? ", preferred" : "") << line_end;
    }
    bool any_counter = false;
    for (const auto &[name, value] : Luminary::DbgCounters::inst().values())
        if (name.rfind("RENDER_", 0) == 0 && value > 0)
        {
            if (!any_counter)
                out << b_start << "Render fallbacks:" << b_end << line_end;
            any_counter = true;
            out << "  " << name << " = " << value << line_end;
        }
    return out.str();
}

} // namespace RenderDiagnostics
} // namespace DSKY
