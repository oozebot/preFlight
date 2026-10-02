///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "RenderCrashGuard.hpp"

#include "I18N.hpp"
#include "format.hpp"
#include "luminary/core/diagnostics/DebugCounters.hpp"
#include "luminary/platform/paths/Paths.hpp"
#include "luminary/platform/process/Process.hpp"
#include "luminary/presets/app_config/AppConfig.hpp"

#include <boost/filesystem/operations.hpp>
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cstdlib>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace DSKY
{
using Luminary::AppConfig;

namespace
{

constexpr size_t MARKER_MAX_BYTES = 4096;

const char *const KEY_LIGHTING = "canvas_lighting_quality";
const char *const KEY_SSAA = "canvas_ssaa_scale";
const char *const KEY_MSAA = "canvas_msaa";

// Each value's GPU cost in order, to tell a lighter setting from a heavier one
int lighting_weight(const std::string &value)
{
    return value == "basic" ? 0 : value == "full" ? 2 : 1;
}

double ssaa_weight(const std::string &value)
{
    char *end = nullptr;
    const double scale = std::strtod(value.c_str(), &end);
    return end != value.c_str() && *end == '\0' && scale > 1.0 ? scale : 1.0;
}

// Auto takes up to 8x
int msaa_weight(const std::string &value)
{
    if (value == "0")
        return 0;
    const int samples = std::atoi(value.c_str());
    return samples > 0 ? samples : 8;
}

bool lighter(const std::string &key, const std::string &now, const std::string &before)
{
    if (key == KEY_LIGHTING)
        return lighting_weight(now) < lighting_weight(before);
    if (key == KEY_SSAA)
        return ssaa_weight(now) < ssaa_weight(before);
    return msaa_weight(now) < msaa_weight(before);
}

RenderCrashGuard::Settings settings_of(const AppConfig &config, const std::string &msaa)
{
    return {config.get(KEY_LIGHTING), config.get(KEY_SSAA), msaa};
}

std::string describe(const RenderCrashGuard::Settings &settings)
{
    return "lighting=" + settings.lighting + ", ssaa=" + settings.ssaa + ", msaa=" + settings.msaa;
}

std::string marker_text(int rung, const RenderCrashGuard::Settings &settings)
{
    std::ostringstream out;
    out << "rung=" << rung << "\n"
        << "lighting=" << settings.lighting << "\n"
        << "ssaa=" << settings.ssaa << "\n"
        << "msaa=" << settings.msaa << "\n";
    return out.str();
}

// A marker that cannot be read is still a session that ended while drawing; its rung is 0 and its settings unknown
void parse_marker(const std::string &text, int &rung, RenderCrashGuard::Settings &settings)
{
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);)
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key == "rung")
            rung = std::clamp(std::atoi(value.c_str()), 0, RenderCrashGuard::MAX_RUNG);
        else if (key == "lighting")
            settings.lighting = value;
        else if (key == "ssaa")
            settings.ssaa = value;
        else if (key == "msaa")
            settings.msaa = value;
    }
}

// Lowers the render settings in AppConfig to the rung; true when a value changed. Rungs are cumulative, so a rescue
// whose lowered values never reached the disk is repeated by the next rung.
bool apply_rung(AppConfig &config, int rung)
{
    bool changed = false;
    auto lower = [&config, &changed](const char *key, const std::string &value)
    {
        if (config.get(key) != value)
        {
            config.set(key, value);
            changed = true;
        }
    };
    if (config.get(KEY_LIGHTING) == "full")
        lower(KEY_LIGHTING, "enhanced");
    if (ssaa_weight(config.get(KEY_SSAA)) > 1.0)
        lower(KEY_SSAA, "off");
    if (msaa_weight(config.get(KEY_MSAA)) > msaa_weight("auto"))
        lower(KEY_MSAA, "auto");
    if (rung >= 2)
    {
        lower(KEY_LIGHTING, "basic");
        lower(KEY_MSAA, "0");
    }
    return changed;
}

std::string setting_label(const std::string &key)
{
    if (key == KEY_LIGHTING)
        return _u8L("Lighting quality");
    if (key == KEY_SSAA)
        return _u8L("Supersampling (SSAA)");
    return _u8L("Anti-aliasing (MSAA)");
}

// The value as the Performance tab's dropdown names it
std::string value_label(const std::string &key, const std::string &value)
{
    if (key == KEY_LIGHTING)
    {
        if (value == "basic")
            return _u8L("Basic");
        if (value == "enhanced")
            return _u8L("Enhanced");
        if (value == "full")
            return _u8L("Full (shadows + AO)");
        if (value == "auto")
            return _u8L("Auto (detect GPU)");
        return value;
    }
    if (key == KEY_SSAA)
    {
        if (value == "off")
            return _u8L("Off");
        if (value == "1.5")
            return _u8L("1.5x");
        if (value == "2")
            return _u8L("2x");
        return value;
    }
    if (value == "auto")
        return _u8L("Auto (detect GPU)");
    if (value == "0")
        return _u8L("Off");
    if (value == "2")
        return _u8L("2x");
    if (value == "4")
        return _u8L("4x");
    if (value == "8")
        return _u8L("8x");
    if (value == "16")
        return _u8L("16x");
    return value;
}

#ifdef _WIN32

// Held with no share for writing or deleting: a process that can open it with delete access knows its owner is gone
HANDLE create_held(const boost::filesystem::path &path)
{
    return ::CreateFileW(path.wstring().c_str(), GENERIC_WRITE | DELETE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
}

// Replaces the content and flushes it to the disk
bool rewrite(HANDLE handle, const std::string &text)
{
    LARGE_INTEGER zero{};
    DWORD written = 0;
    return ::SetFilePointerEx(handle, zero, nullptr, FILE_BEGIN) && ::SetEndOfFile(handle) &&
           ::WriteFile(handle, text.data(), DWORD(text.size()), &written, nullptr) && written == DWORD(text.size()) &&
           ::FlushFileBuffers(handle);
}

// Marked for deletion while still held, so no other process can open it in between, then closed
bool delete_held(HANDLE handle, const boost::filesystem::path &path)
{
    FILE_DISPOSITION_INFO disposition{TRUE};
    const BOOL marked = ::SetFileInformationByHandle(handle, FileDispositionInfo, &disposition, sizeof(disposition));
    ::CloseHandle(handle);
    // Wine lacks the disposition call
    return marked || ::DeleteFileW(path.wstring().c_str()) != 0;
}

// Opens a marker only when no live process holds it
HANDLE open_dead(const boost::filesystem::path &path)
{
    return ::CreateFileW(path.wstring().c_str(), GENERIC_READ | DELETE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
}

std::string read_all(HANDLE handle)
{
    std::string text(MARKER_MAX_BYTES, '\0');
    DWORD read = 0;
    if (!::ReadFile(handle, text.data(), DWORD(text.size()), &read, nullptr))
        read = 0;
    text.resize(read);
    return text;
}

#else

// Replaces the content and flushes it to the disk
bool rewrite(int fd, const std::string &text)
{
    if (::ftruncate(fd, 0) != 0)
        return false;
    size_t done = 0;
    while (done < text.size())
    {
        const ssize_t n = ::pwrite(fd, text.data() + done, text.size() - done, off_t(done));
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        done += size_t(n);
    }
    return ::fsync(fd) == 0;
}

// The directory entry of a renamed file reaches the disk with the directory
void sync_directory(const boost::filesystem::path &dir)
{
    const int fd = ::open(dir.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd >= 0)
    {
        ::fsync(fd);
        ::close(fd);
    }
}

// Locked for the life of the process: a process that can lock it knows its owner is gone. It is written and locked
// under a temporary name first, so no other process ever finds it unlocked while its owner lives.
int create_held(const boost::filesystem::path &path, const std::string &text)
{
    const boost::filesystem::path temp = path.string() + ".tmp";
    const int fd = ::open(temp.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0)
        return -1;
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0 || !rewrite(fd, text) || ::rename(temp.c_str(), path.c_str()) != 0)
    {
        ::unlink(temp.c_str());
        ::close(fd);
        return -1;
    }
    sync_directory(path.parent_path());
    return fd;
}

// Unlinked while still locked, so no other process can open it in between, then closed
bool delete_held(int fd, const boost::filesystem::path &path)
{
    const bool removed = ::unlink(path.c_str()) == 0;
    ::close(fd);
    return removed;
}

// Opens a marker only when no live process holds its lock
int open_dead(const boost::filesystem::path &path)
{
    const int fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return -1;
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0)
    {
        ::close(fd);
        return -1;
    }
    return fd;
}

std::string read_all(int fd)
{
    std::string text(MARKER_MAX_BYTES, '\0');
    const ssize_t n = ::pread(fd, text.data(), text.size(), 0);
    text.resize(n > 0 ? size_t(n) : 0);
    return text;
}

#endif // _WIN32

} // namespace

RenderCrashGuard &RenderCrashGuard::get()
{
    static RenderCrashGuard s_guard;
    return s_guard;
}

RenderCrashGuard::Settings RenderCrashGuard::current_settings(const AppConfig &config) const
{
    return settings_of(config, m_session_msaa);
}

void RenderCrashGuard::start_session(AppConfig &config, bool editor)
{
    // Outside the cache folder, which a configuration import copies from another installation
    m_dir = boost::filesystem::path(Luminary::data_dir()) / "renderer";
    m_prefix = editor ? "editor_" : "gcodeviewer_";
    m_path = m_dir / (m_prefix + std::to_string(Luminary::get_current_pid()) + ".ini");

    const std::vector<Marker> dead = take_dead_markers();
    if (!dead.empty())
    {
        // Instances that ended together leave one marker each; the highest rung carries on
        const auto crashed = std::max_element(dead.begin(), dead.end(),
                                              [](const Marker &a, const Marker &b) { return a.rung < b.rung; });
        rescue(config, *crashed);
    }

    m_session_msaa = config.get(KEY_MSAA);
    m_started = true;
    // The canvases' MSAA probe, the GL context and the shader compilation all come before the first frame
    arm(current_settings(config));
}

void RenderCrashGuard::end_session()
{
    delete_marker();
    m_started = false;
}

void RenderCrashGuard::disarm()
{
    delete_marker();
}

std::vector<RenderCrashGuard::Marker> RenderCrashGuard::take_dead_markers() const
{
    std::vector<boost::filesystem::path> candidates;
    boost::system::error_code ec;
    for (boost::filesystem::directory_iterator it(m_dir, ec), end; !ec && it != end; it.increment(ec))
    {
        const std::string name = it->path().filename().string();
        if (name.size() > m_prefix.size() && name.compare(0, m_prefix.size(), m_prefix) == 0 &&
            it->path().extension().string() == ".ini")
            candidates.push_back(it->path());
    }

    std::vector<Marker> dead;
    for (const boost::filesystem::path &path : candidates)
    {
        // A marker its process still holds belongs to a running session
#ifdef _WIN32
        const HANDLE handle = open_dead(path);
        if (handle == INVALID_HANDLE_VALUE)
            continue;
        const std::string text = read_all(handle);
        const bool deleted = delete_held(handle, path);
#else
        const int fd = open_dead(path);
        if (fd < 0)
            continue;
        const std::string text = read_all(fd);
        const bool deleted = delete_held(fd, path);
#endif
        if (!deleted)
            BOOST_LOG_TRIVIAL(error) << "Render crash guard: the marker " << path.string()
                                     << " could not be deleted and will be read again at the next launch";
        Marker marker;
        parse_marker(text, marker.rung, marker.settings);
        dead.push_back(marker);
    }
    return dead;
}

void RenderCrashGuard::rescue(AppConfig &config, const Marker &crashed)
{
    const Settings before = settings_of(config, config.get(KEY_MSAA));
    // Past the last rung nothing changes
    const bool past_last_rung = crashed.rung >= MAX_RUNG;
    int rung = MAX_RUNG;
    if (!past_last_rung)
    {
        rung = crashed.rung + 1;
        bool changed = apply_rung(config, rung);
        // A rung that changes nothing would start this session with the settings that just ended one
        if (!changed && rung < MAX_RUNG && crashed.settings == before)
            changed = apply_rung(config, ++rung);
        // On the disk before the canvases are created, in case this session ends the same way
        if (changed)
            config.save();
    }
    m_rung = rung;

    const Settings after = settings_of(config, config.get(KEY_MSAA));
    // Named: each setting now lighter than the session that ended drew with, or than AppConfig held
    const std::pair<const char *, std::string Settings::*> keys[] = {{KEY_LIGHTING, &Settings::lighting},
                                                                     {KEY_SSAA, &Settings::ssaa},
                                                                     {KEY_MSAA, &Settings::msaa}};
    if (!past_last_rung)
        for (const auto &[key, member] : keys)
            if (lighter(key, after.*member, crashed.settings.*member) || lighter(key, after.*member, before.*member))
                m_lowered.emplace_back(key, after.*member);

    if (m_lowered.empty())
    {
        // Past the last rung, or already as low as the ladder goes: this session starts as the one that ended
        DBG_COUNT_LOAD("RENDER_CRASH_GUARD_EXHAUSTED");
        BOOST_LOG_TRIVIAL(error) << "Render crash guard: the previous session ended while drawing with "
                                 << describe(crashed.settings) << " at rescue rung " << crashed.rung
                                 << "; nothing lower is left, render settings stay " << describe(after);
        return;
    }
    DBG_COUNT_LOAD("RENDER_CRASH_GUARD_RESCUE");
    BOOST_LOG_TRIVIAL(warning) << "Render crash guard: the previous session ended while drawing with "
                               << describe(crashed.settings) << "; rescue rung " << rung << " changed "
                               << describe(before) << " to " << describe(after);
}

void RenderCrashGuard::arm(const Settings &settings)
{
    m_armed = true;
    m_armed_settings = settings;
    m_frames_run = 0;
    if (write_marker(settings))
    {
        BOOST_LOG_TRIVIAL(debug) << "Render crash guard: armed for " << describe(settings) << ", rung " << m_rung;
        return;
    }
    // These settings run unguarded: a crash under them is not rescued at the next launch
    DBG_COUNT_LOAD("RENDER_CRASH_GUARD_WRITE_FAILED");
    BOOST_LOG_TRIVIAL(error) << "Render crash guard: the marker " << m_path.string() << " could not be written";
}

bool RenderCrashGuard::write_marker(const Settings &settings)
{
    const std::string text = marker_text(m_rung, settings);
#ifdef _WIN32
    if (m_handle == nullptr)
    {
        boost::system::error_code ec;
        boost::filesystem::create_directories(m_dir, ec);
        const HANDLE handle = create_held(m_path);
        if (handle == INVALID_HANDLE_VALUE)
            return false;
        m_handle = handle;
    }
    return rewrite(static_cast<HANDLE>(m_handle), text);
#else
    if (m_fd < 0)
    {
        boost::system::error_code ec;
        boost::filesystem::create_directories(m_dir, ec);
        m_fd = create_held(m_path, text);
        return m_fd >= 0;
    }
    return rewrite(m_fd, text);
#endif
}

void RenderCrashGuard::delete_marker()
{
    m_armed = false;
    m_frames_run = 0;
    m_frame_armed = false;
#ifdef _WIN32
    if (m_handle == nullptr)
        return;
    const bool deleted = delete_held(static_cast<HANDLE>(m_handle), m_path);
    m_handle = nullptr;
#else
    if (m_fd < 0)
        return;
    const bool deleted = delete_held(m_fd, m_path);
    m_fd = -1;
#endif
    if (!deleted)
    {
        // The next launch reads the marker as a session that ended while drawing
        DBG_COUNT_LOAD("RENDER_CRASH_GUARD_DELETE_FAILED");
        BOOST_LOG_TRIVIAL(error) << "Render crash guard: the marker " << m_path.string() << " could not be deleted";
    }
}

void RenderCrashGuard::before_frame(const AppConfig *config)
{
    m_frame_armed = false;
    if (!m_started || config == nullptr)
        return;
    const Settings settings = current_settings(*config);
    if (m_proven_valid && settings == m_proven)
    {
        // Back on proven settings: the armed ones are no longer drawn
        if (m_armed)
            delete_marker();
        return;
    }
    if (!m_armed || settings != m_armed_settings)
        arm(settings);
    m_frame_armed = true;

#ifdef PREFLIGHT_TEST_HOOKS
    // PREFLIGHT_RENDER_CRASH_AFTER_ARM=1: the process dies before the first frame drawn under armed settings, with
    // the marker on the disk, as a driver crash on that frame would
    static const bool crash_after_arm = []()
    {
        const char *env = std::getenv("PREFLIGHT_RENDER_CRASH_AFTER_ARM");
        return env != nullptr && std::string(env) == "1";
    }();
#ifdef _WIN32
    const bool marker_held = m_handle != nullptr;
#else
    const bool marker_held = m_fd >= 0;
#endif
    if (crash_after_arm && marker_held)
    {
        BOOST_LOG_TRIVIAL(warning) << "Render crash guard: aborting after arming (PREFLIGHT_RENDER_CRASH_AFTER_ARM)";
#ifdef _MSC_VER
        // No abort message box or error report dialog to hold the test up
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
        std::abort();
    }
#endif // PREFLIGHT_TEST_HOOKS
}

bool RenderCrashGuard::after_frame(bool ran)
{
    if (!m_frame_armed)
        return false;
    m_frame_armed = false;
    if (!ran)
        return false;
    if (++m_frames_run < PROVING_FRAMES)
        return true;

    m_proven = m_armed_settings;
    m_proven_valid = true;
    m_rung = 0;
    BOOST_LOG_TRIVIAL(debug) << "Render crash guard: proven " << describe(m_proven);
    delete_marker();
    return false;
}

std::string RenderCrashGuard::rescue_notice() const
{
    if (m_lowered.empty())
        return {};
    std::string text = _u8L(
        "preFlight closed unexpectedly while drawing the 3D view. These render settings were lowered:");
    for (const auto &[key, value] : m_lowered)
        text += "\n" + setting_label(key) + ": " + value_label(key, value);
    text += "\n" + format(_u8L("They can be changed back in %1%."), _u8L("Preferences") + " > " + _u8L("Performance"));
    return text;
}

} // namespace DSKY
