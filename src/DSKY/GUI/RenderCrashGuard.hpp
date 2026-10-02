///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include <boost/filesystem/path.hpp>

#include <string>
#include <utility>
#include <vector>

namespace Luminary
{
class AppConfig;
}

namespace DSKY
{

// Keeps a render setting that crashes the graphics driver on its first frames from crashing every later launch.
// Before the first frames under render settings this process has not proven, a marker file naming them is written
// and flushed to disk; after PROVING_FRAMES completed frames that ran them it is deleted. The process holds its
// marker locked while it lives, so a marker found unlocked at the next launch means that session ended while
// drawing with those settings: the launch lowers them one rung before any canvas or GL context exists, and a crash
// under the lowered settings climbs to the next rung. Used from the UI thread only.
class RenderCrashGuard
{
public:
    // The render preferences a frame is drawn with, as AppConfig holds them
    struct Settings
    {
        std::string lighting; // canvas_lighting_quality
        std::string ssaa;     // canvas_ssaa_scale
        std::string msaa;     // canvas_msaa the canvases were created with

        bool operator==(const Settings &rhs) const
        {
            return lighting == rhs.lighting && ssaa == rhs.ssaa && msaa == rhs.msaa;
        }
        bool operator!=(const Settings &rhs) const { return !(*this == rhs); }
    };

    // Completed frames that ran the armed settings before they count as proven
    static constexpr int PROVING_FRAMES = 3;
    // The last rung of the rescue ladder: rung 1 turns Full lighting, supersampling and MSAA above Auto's cap down,
    // rung 2 also sets Basic lighting and MSAA off
    static constexpr int MAX_RUNG = 2;

    static RenderCrashGuard &get();

    // Startup, after AppConfig is loaded and before any canvas or GL context exists: lowers the render settings of a
    // session that ended while drawing with unproven ones, then arms for this session's first frames
    void start_session(Luminary::AppConfig &config, bool editor);
    // A clean exit or a failed start: deletes the marker and stops guarding
    void end_session();
    // The process image is about to be replaced: deletes the marker, and guarding goes on if it is not
    void disarm();

    // Before a frame's first GL work: arms when the frame's settings are neither proven nor already armed
    void before_frame(const Luminary::AppConfig *config);
    // After the swap returned. `ran` is false when a temporary block kept a requested feature from running, and then
    // the frame does not count. Returns true while the armed settings still need frames that run them.
    bool after_frame(bool ran);

    // What the startup rescue lowered, as notification text; empty when nothing was lowered
    std::string rescue_notice() const;

private:
    struct Marker
    {
        int rung{0};
        Settings settings;
    };

    RenderCrashGuard() = default;
    RenderCrashGuard(const RenderCrashGuard &) = delete;
    RenderCrashGuard &operator=(const RenderCrashGuard &) = delete;

    Settings current_settings(const Luminary::AppConfig &config) const;
    // Markers of this app mode whose process is gone; each is read and deleted
    std::vector<Marker> take_dead_markers() const;
    void rescue(Luminary::AppConfig &config, const Marker &crashed);
    void arm(const Settings &settings);
    bool write_marker(const Settings &settings);
    void delete_marker();

    bool m_started{false};
    boost::filesystem::path m_dir;
    // "editor_" or "gcodeviewer_": each app mode rescues the AppConfig it reads
    std::string m_prefix;
    boost::filesystem::path m_path;
    // The canvases' pixel format is chosen once per process, from this value
    std::string m_session_msaa;
    // The rescue rung this session started at, carried into every marker until settings are proven
    int m_rung{0};

    bool m_proven_valid{false};
    Settings m_proven;
    bool m_armed{false};
    Settings m_armed_settings;
    int m_frames_run{0};
    // The frame in flight is drawn with the armed settings
    bool m_frame_armed{false};

    // Rescued settings the notice names: the AppConfig key and its value now
    std::vector<std::pair<std::string, std::string>> m_lowered;

    // The held marker: a HANDLE on Windows, a file descriptor elsewhere
#ifdef _WIN32
    void *m_handle{nullptr};
#else
    int m_fd{-1};
#endif
};

} // namespace DSKY
