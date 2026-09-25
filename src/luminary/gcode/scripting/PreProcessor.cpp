///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "PreProcessor.hpp"
#include "luminary/gcode/interpret/GCodeProcessor.hpp"
#include "luminary/gcode/interpret/GCodeObject.hpp"
#include "luminary/layer/print/Print.hpp"
#include "luminary/platform/paths/Paths.hpp"
#include "luminary/core/diagnostics/DebugCounters.hpp"
#include "PreFlightVersion.h"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/embed.h>
#include <pybind11/functional.h>

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>

#include <cctype>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <exception> // std::exception, which the precompiled header had been supplying
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <string_view>
#include <unordered_set>

namespace py = pybind11;

namespace Luminary
{

// -------------------------------------------------------------------------
// Layer wrapper: groups moves by layer_id and provides prepend/append
// -------------------------------------------------------------------------
// Substring scan over a 1-based inclusive line range of the virtual file (end 0 = last line). Lines
// are searched as views, no copies. Returns the first hit, or appends every hit to `all` and returns 0.
static unsigned int scan_lines(const GCodeObject *gco, const std::string &text, unsigned int start, unsigned int end,
                               py::list *all)
{
    if (gco == nullptr)
        return 0;
    const size_t count = gco->line_count();
    if (end == 0 || end > count)
        end = static_cast<unsigned int>(count);
    if (start == 0)
        start = 1;
    for (size_t i = start; i <= end; ++i)
    {
        const std::string_view line = gco->get_line_text(i - 1);
        if (line.find(text) != std::string_view::npos)
        {
            if (all == nullptr)
                return static_cast<unsigned int>(i);
            all->append(static_cast<unsigned int>(i));
        }
    }
    return 0;
}

struct PyLayer
{
    unsigned int id;
    float z;
    float height;
    std::vector<GCodeProcessorResult::MoveVertex *> moves;
    float time;

    // Raw text range of this layer in the virtual file (1-based, inclusive): from the line after the
    // previous layer's last move through this layer's last move, so the layer-change block that
    // introduces the layer belongs to it. Empty (last_line < first_line) for a layer without moves.
    unsigned int first_line = 0;
    unsigned int last_line = 0;
    const GCodeObject *gco = nullptr;

    unsigned int find_line(const std::string &text) const
    {
        return last_line >= first_line ? scan_lines(gco, text, first_line, last_line, nullptr) : 0;
    }
    py::list find_lines(const std::string &text) const
    {
        py::list found;
        if (last_line >= first_line)
            scan_lines(gco, text, first_line, last_line, &found);
        return found;
    }
    py::list lines() const
    {
        py::list out;
        if (gco != nullptr && last_line >= first_line)
            for (unsigned int i = first_line; i <= last_line && i <= gco->line_count(); ++i)
                out.append(py::make_tuple(i, std::string(gco->get_line_text(i - 1))));
        return out;
    }

    // G-code to insert before/after this layer in the virtual file
    std::string prepend_gcode;
    std::string append_gcode;

    void prepend(const std::string &gcode, const std::string &comment = "")
    {
        prepend_gcode += gcode;
        if (!comment.empty())
            prepend_gcode += " ; " + comment;
        prepend_gcode += "\n";
    }
    void append(const std::string &gcode, const std::string &comment = "")
    {
        append_gcode += gcode;
        if (!comment.empty())
            append_gcode += " ; " + comment;
        append_gcode += "\n";
    }

    // Convenience: filter moves by type
    std::vector<GCodeProcessorResult::MoveVertex *> moves_by_type(EMoveType move_type) const
    {
        std::vector<GCodeProcessorResult::MoveVertex *> result;
        for (auto *mv : moves)
            if (mv->type == move_type)
                result.push_back(mv);
        return result;
    }

    // Convenience: filter moves by extrusion role
    std::vector<GCodeProcessorResult::MoveVertex *> moves_by_role(GCodeExtrusionRole role) const
    {
        std::vector<GCodeProcessorResult::MoveVertex *> result;
        for (auto *mv : moves)
            if (mv->extrusion_role == role)
                result.push_back(mv);
        return result;
    }

    // Total filament extruded in this layer (mm)
    float extrusion_length() const
    {
        float total = 0.0f;
        for (const auto *mv : moves)
            if (mv->delta_extruder > 0.0f)
                total += mv->delta_extruder;
        return total;
    }

    // Total travel (non-extrusion) distance in this layer (mm)
    float travel_distance() const
    {
        float total = 0.0f;
        for (size_t i = 1; i < moves.size(); ++i)
        {
            if (moves[i]->type == EMoveType::Travel)
            {
                float dx = moves[i]->position.x() - moves[i - 1]->position.x();
                float dy = moves[i]->position.y() - moves[i - 1]->position.y();
                total += std::sqrt(dx * dx + dy * dy);
            }
        }
        return total;
    }
};

// -------------------------------------------------------------------------
// GCode wrapper: top-level object passed to process()
// -------------------------------------------------------------------------
struct PyGCode
{
    GCodeProcessorResult *result;
    GCodeObject *gcode_object;
    const Print *print;
    std::vector<PyLayer> layers;

    // Populated from result
    float max_print_height;
    size_t extruder_count;
    std::vector<std::string> extruder_colors;
    bool spiral_vase_mode;

    // Global annotation appended to modified G-code lines (fallback for moves without per-move annotation).
    std::string annotation;

    // Per-move annotations keyed by MoveVertex pointer (set via move.annotation = "...")
    std::unordered_map<const GCodeProcessorResult::MoveVertex *, std::string> move_annotations;

    // G-code insertions: line_id -> gcode text (after or before)
    std::map<unsigned int, std::string> insertions_after;
    std::map<unsigned int, std::string> insertions_before;

    // Line replacements: line_id -> new text
    std::unordered_map<unsigned int, std::string> line_replacements;

    // Moves marked for removal (by gcode_id)
    std::unordered_set<unsigned int> removed_move_ids;
    // Bumped by every removal and rebuild; gcode.moves is served from a list cached per version
    // so repeated access stays cheap while removed and dead entries stay hidden.
    size_t removal_version = 0;
    PyObject *moves_cache = nullptr;
    size_t moves_cache_version = 0;
    // gcode.settings built once per slice: the merged config does not change while scripts run
    PyObject *settings_cache = nullptr;

    // A move a script may still see and edit: parsed from a real line and not removed by this script.
    bool is_live(const GCodeProcessorResult::MoveVertex &mv) const
    {
        return mv.gcode_id != 0 && removed_move_ids.count(mv.gcode_id) == 0;
    }

    // Must run with the GIL held. Releases every cached Python object this wrapper holds.
    void release_moves_cache()
    {
        Py_XDECREF(moves_cache);
        moves_cache = nullptr;
        Py_XDECREF(settings_cache);
        settings_cache = nullptr;
    }

    // Build layer structure from flat moves vector
    void build_layers()
    {
        ++removal_version;
        layers.clear();
        if (result->moves.empty())
            return;

        unsigned int max_layer = 0;
        for (auto &mv : result->moves)
            if (mv.gcode_id != 0 && mv.layer_id > max_layer)
                max_layer = mv.layer_id;

        constexpr unsigned int MAX_LAYERS = 100000;
        if (max_layer > MAX_LAYERS)
        {
            BOOST_LOG_TRIVIAL(warning) << "Pre-processor: layer_id " << max_layer << " exceeds limit, clamping to "
                                       << MAX_LAYERS;
            max_layer = MAX_LAYERS;
        }

        layers.resize(max_layer + 1);
        for (unsigned int i = 0; i <= max_layer; ++i)
        {
            layers[i].id = i;
            layers[i].z = 0.0f;
            layers[i].height = 0.0f;
            layers[i].time = 0.0f;
        }

        // layer.z is the Z the layer prints at: the highest Z among its extruding moves. Travels
        // are only used for layers without any extrusion, so a z-hop cannot inflate the value.
        std::vector<float> travel_z(layers.size(), 0.0f);
        std::vector<bool> has_extrusion(layers.size(), false);
        for (auto &mv : result->moves)
        {
            if (mv.gcode_id == 0 || mv.layer_id > max_layer)
                continue;
            auto &layer = layers[mv.layer_id];
            layer.moves.push_back(&mv);
            layer.time += mv.time[0];
            if (mv.type == EMoveType::Extrude)
            {
                has_extrusion[mv.layer_id] = true;
                if (mv.position.z() > layer.z)
                    layer.z = mv.position.z();
            }
            else if (mv.position.z() > travel_z[mv.layer_id])
                travel_z[mv.layer_id] = mv.position.z();
        }
        for (size_t i = 0; i < layers.size(); ++i)
            if (!has_extrusion[i])
                layers[i].z = travel_z[i];

        // Compute layer heights as deltas
        for (size_t i = 1; i < layers.size(); ++i)
            layers[i].height = layers[i].z - layers[i - 1].z;
        if (!layers.empty())
            layers[0].height = layers[0].z;

        // Line ranges: moves are in file order, so each layer owns the text from the line after the
        // previous layer's last move through its own last move. Text after the final layer's last
        // move (end G-code) belongs to no layer.
        unsigned int prev_last = 0;
        for (auto &layer : layers)
        {
            layer.gco = gcode_object;
            unsigned int last = prev_last;
            for (const auto *mv : layer.moves)
                last = std::max(last, mv->gcode_id);
            layer.first_line = prev_last + 1;
            layer.last_line = last;
            prev_last = last;
        }

        // Populate top-level fields
        max_print_height = result->max_print_height;
        extruder_count = result->extruders_count;
        extruder_colors = result->extruder_colors;
        spiral_vase_mode = result->spiral_vase_mode;
    }

    float time_estimate_normal() const { return result->print_statistics.modes[0].time; }

    float time_estimate_stealth() const { return result->print_statistics.modes[1].time; }

    float first_layer_time() const
    {
        if (layers.empty())
            return 0.0f;
        // Layer 0 may be empty (pre-print moves), layer 1 is typically first print layer
        for (const auto &layer : layers)
            if (layer.time > 0.0f)
                return layer.time;
        return 0.0f;
    }

    void insert(unsigned int line, const std::string &gcode, const std::string &position = "after",
                const std::string &comment = "")
    {
        // Bad calls raise into the script so the author sees them, instead of a log line nobody reads
        size_t total_lines = gcode_object ? gcode_object->line_count() : 0;
        if (line == 0 || line > total_lines)
            throw py::value_error("insert(): line " + std::to_string(line) + " is out of range (the file has " +
                                  std::to_string(total_lines) + " lines)");
        if (position != "before" && position != "after")
            throw py::value_error("insert(): position must be 'before' or 'after', got '" + position + "'");
        std::string text = gcode;
        if (!comment.empty())
            text += " ; " + comment;
        auto &target = (position == "before") ? insertions_before : insertions_after;
        auto it = target.find(line);
        if (it != target.end())
            it->second += "\n" + text;
        else
            target[line] = text;
    }

    std::string get_line(unsigned int line_id) const
    {
        if (line_id == 0 || !gcode_object || (line_id - 1) >= gcode_object->line_count())
            return "";
        return std::string(gcode_object->get_line_text(line_id - 1));
    }

    void rewrite(unsigned int line_id, const std::string &gcode, const std::string &comment = "")
    {
        size_t total_lines = gcode_object ? gcode_object->line_count() : 0;
        if (line_id == 0 || line_id > total_lines)
            throw py::value_error("rewrite(): line " + std::to_string(line_id) + " is out of range (the file has " +
                                  std::to_string(total_lines) + " lines)");
        if (comment.empty())
            line_replacements[line_id] = gcode;
        else
            line_replacements[line_id] = gcode + " ; " + comment;
    }

    unsigned int line_count() const { return gcode_object ? static_cast<unsigned int>(gcode_object->line_count()) : 0; }

    // 1-based line_id of the first line containing text within start..end (end 0 = last line), 0 if none
    unsigned int find_line(const std::string &text, unsigned int start = 1, unsigned int end = 0) const
    {
        return scan_lines(gcode_object, text, start, end, nullptr);
    }

    // All 1-based line_ids containing text within start..end
    py::list find_lines(const std::string &text, unsigned int start = 1, unsigned int end = 0) const
    {
        py::list found;
        scan_lines(gcode_object, text, start, end, &found);
        return found;
    }

    int remove_moves(py::function predicate)
    {
        int count = 0;
        for (auto &mv : result->moves)
        {
            if (!is_live(mv))
                continue;
            if (predicate(py::cast(&mv, py::return_value_policy::reference)).cast<bool>())
            {
                removed_move_ids.insert(mv.gcode_id);
                ++count;
            }
        }
        if (count > 0)
        {
            // Removed moves disappear from every listing at once, not only at materialization.
            ++removal_version;
            for (auto &layer : layers)
                layer.moves.erase(std::remove_if(layer.moves.begin(), layer.moves.end(),
                                                 [this](const GCodeProcessorResult::MoveVertex *mv)
                                                 { return !is_live(*mv); }),
                                  layer.moves.end());
        }
        return count;
    }

    py::list find_moves(py::kwargs kwargs)
    {
        py::list found;
        std::optional<EMoveType> filter_type;
        std::optional<GCodeExtrusionRole> filter_role;
        std::optional<int> filter_extruder;
        std::optional<float> filter_z_min;
        std::optional<float> filter_z_max;

        if (kwargs.contains("type"))
            filter_type = kwargs["type"].cast<EMoveType>();
        if (kwargs.contains("role"))
            filter_role = kwargs["role"].cast<GCodeExtrusionRole>();
        if (kwargs.contains("extruder"))
            filter_extruder = kwargs["extruder"].cast<int>();
        if (kwargs.contains("z_min"))
            filter_z_min = kwargs["z_min"].cast<float>();
        if (kwargs.contains("z_max"))
            filter_z_max = kwargs["z_max"].cast<float>();

        for (auto &mv : result->moves)
        {
            if (!is_live(mv))
                continue;
            if (filter_type && mv.type != *filter_type)
                continue;
            if (filter_role && mv.extrusion_role != *filter_role)
                continue;
            if (filter_extruder && mv.extruder_id != *filter_extruder)
                continue;
            if (filter_z_min && mv.position.z() < *filter_z_min)
                continue;
            if (filter_z_max && mv.position.z() > *filter_z_max)
                continue;
            found.append(py::cast(&mv, py::return_value_policy::reference));
        }
        return found;
    }
};

// Active PyGCode instance for per-move annotation access from Move bindings.
// Set before each script runs, cleared after. Single-threaded (GIL held).
static PyGCode *s_active_gcode = nullptr;

// The script API version this build implements. Bumped only when a published binding changes
// meaning or disappears; additions keep the number. A script may declare the version it was
// written for in a header comment, "# preflight-api: N", and is refused when N is newer.
static constexpr int s_api_version = 1;

// Module names a script file must never be called: the script directory is on sys.path, so a
// script named like a standard-library module would shadow it for every later import.
static const std::unordered_set<std::string> s_reserved_module_names = {
    "os",          "sys",        "io",       "re",       "ssl",      "json",   "csv",       "ftplib",
    "socket",      "http",       "urllib",   "email",    "logging",  "math",   "time",      "datetime",
    "threading",   "subprocess", "ctypes",   "struct",   "hashlib",  "base64", "shutil",    "pathlib",
    "tempfile",    "signal",     "queue",    "copy",     "types",    "abc",    "functools", "itertools",
    "collections", "preFlight",  "pybind11", "builtins", "importlib"};

// The API version a script declares in its first lines ("# preflight-api: N"), 0 when absent.
static int declared_api_version(const std::string &script_path)
{
    boost::nowide::ifstream in(script_path);
    std::string line;
    for (int i = 0; i < 20 && std::getline(in, line); ++i)
    {
        std::string lower = line;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
        size_t pos = lower.find("preflight-api");
        if (pos == std::string::npos || lower.find('#') == std::string::npos || lower.find('#') > pos)
            continue;
        pos = lower.find_first_of(":=", pos);
        if (pos == std::string::npos)
            continue;
        try
        {
            return std::stoi(lower.substr(pos + 1));
        }
        catch (...)
        {
            return 0;
        }
    }
    return 0;
}

// Tag type behind the Python ScriptTimeout exception; the watchdog raises the Python type, C++ never throws it.
struct ScriptTimeoutError : std::exception
{
    const char *what() const noexcept override { return "preprocessing script timed out"; }
};
static PyObject *s_script_timeout_type = nullptr;

// Watches one script call from a helper thread. While a call is armed it polls every 100 ms: a
// canceled Print raises KeyboardInterrupt in the executing Python thread, a call past the wall-clock
// limit raises ScriptTimeout. The exception is raised again on every poll, and two seconds after the
// first one a line monitor makes every line the script executes raise, so even a loop that catches
// BaseException unwinds. Delivery needs the GIL, which the executing thread releases at its switch
// interval; a script inside one long native call is interrupted when that call returns.
class ScriptWatchdog
{
public:
    enum class Fired
    {
        None,
        Cancel,
        Timeout
    };

    // canceled: polled while a call is armed; may be empty when nothing can cancel the call.
    ScriptWatchdog(std::function<bool()> canceled, double timeout_seconds)
        : m_canceled(std::move(canceled)), m_timeout_s(timeout_seconds)
    {
        if (m_canceled || m_timeout_s > 0.0)
            m_thread = std::thread([this]() { this->run(); });
    }
    ~ScriptWatchdog() { stop(); }

    // Both called by the executing thread with the GIL held, around the script call.
    void arm()
    {
        m_thread_id = PyThread_get_thread_ident();
        m_start = std::chrono::steady_clock::now();
        m_fired.store(Fired::None, std::memory_order_relaxed);
        m_armed.store(true, std::memory_order_release);
    }
    Fired disarm()
    {
        m_armed.store(false, std::memory_order_release);
        remove_line_monitor();
        // A request that landed after the call returned must not surface in the host's own Python calls.
        PyThreadState_SetAsyncExc(m_thread_id, nullptr);
        return m_fired.load(std::memory_order_relaxed);
    }
    Fired last_fired() const { return m_fired.load(std::memory_order_relaxed); }

    // Joins the helper; releases the GIL meanwhile when this thread holds it, because the helper may
    // be waiting for it.
    void stop()
    {
        m_running.store(false, std::memory_order_relaxed);
        if (!m_thread.joinable())
            return;
        if (PyGILState_Check())
        {
            py::gil_scoped_release release;
            m_thread.join();
        }
        else
            m_thread.join();
    }

private:
    void run()
    {
        while (m_running.load(std::memory_order_relaxed))
        {
            if (m_armed.load(std::memory_order_acquire))
            {
                const bool cancel = m_canceled && m_canceled();
                const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_start).count();
                const bool timeout = m_timeout_s > 0.0 && elapsed > m_timeout_s;
                if (cancel || timeout)
                {
                    PyObject *exc = cancel ? PyExc_KeyboardInterrupt : s_script_timeout_type;
                    PyGILState_STATE gs = PyGILState_Ensure();
                    if (m_armed.load(std::memory_order_acquire))
                    {
                        PyThreadState_SetAsyncExc(m_thread_id, exc);
                        if (m_fired.load(std::memory_order_relaxed) == Fired::None)
                            m_first_fire = std::chrono::steady_clock::now();
                        m_fired.store(cancel ? Fired::Cancel : Fired::Timeout, std::memory_order_relaxed);
                        // An async exception is catchable: a script looping inside "except BaseException"
                        // swallows every one of them. Past a grace period every line it executes raises instead.
                        const double since_fire =
                            std::chrono::duration<double>(std::chrono::steady_clock::now() - m_first_fire).count();
                        if (since_fire > 2.0 && !m_monitor_installed.load(std::memory_order_relaxed))
                            install_line_monitor(exc);
                    }
                    PyGILState_Release(gs);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    // GIL held, watchdog thread. A sys.monitoring LINE callback that raises exc on the executing thread
    // while ARMED; nothing in the script can complete a handler, so the call unwinds.
    void install_line_monitor(PyObject *exc)
    {
        try
        {
            py::dict g;
            g["__builtins__"] = py::module_::import("builtins");
            g["TARGET"] = m_thread_id;
            g["EXC"] = py::reinterpret_borrow<py::object>(exc);
            g["ARMED"] = true;
            py::exec(R"PY(
import sys, threading
_tool = 4
def _pf_watchdog_line(code, line):
    if ARMED and threading.get_ident() == TARGET:
        raise EXC("preprocessing script stopped by the watchdog")
sys.monitoring.use_tool_id(_tool, "preFlight watchdog")
sys.monitoring.register_callback(_tool, sys.monitoring.events.LINE, _pf_watchdog_line)
sys.monitoring.set_events(_tool, sys.monitoring.events.LINE)
)PY",
                     g);
            m_monitor_globals = g.release().ptr();
            m_monitor_installed.store(true, std::memory_order_relaxed);
            DBG_COUNT("PP_SCRIPT_LINE_MONITOR");
        }
        catch (const std::exception &e)
        {
            BOOST_LOG_TRIVIAL(warning) << "Pre-processor: watchdog line monitor unavailable: " << e.what();
        }
    }

    // GIL held, executing thread. ARMED is cleared through the C API first so the removal code, which runs
    // on the monitored thread, is not itself interrupted.
    void remove_line_monitor()
    {
        if (!m_monitor_installed.exchange(false, std::memory_order_relaxed))
            return;
        py::dict g = py::reinterpret_steal<py::dict>(m_monitor_globals);
        m_monitor_globals = nullptr;
        PyDict_SetItemString(g.ptr(), "ARMED", Py_False);
        try
        {
            py::exec(R"PY(
import sys
_tool = 4
if sys.monitoring.get_tool(_tool) == "preFlight watchdog":
    sys.monitoring.set_events(_tool, 0)
    sys.monitoring.register_callback(_tool, sys.monitoring.events.LINE, None)
    sys.monitoring.free_tool_id(_tool)
)PY",
                     g);
        }
        catch (const std::exception &e)
        {
            BOOST_LOG_TRIVIAL(warning) << "Pre-processor: watchdog line monitor removal failed: " << e.what();
        }
    }

    std::function<bool()> m_canceled;
    std::atomic<bool> m_monitor_installed{false};
    PyObject *m_monitor_globals = nullptr;
    std::chrono::steady_clock::time_point m_first_fire;
    double m_timeout_s;
    std::thread m_thread;
    std::atomic<bool> m_running{true};
    std::atomic<bool> m_armed{false};
    std::atomic<Fired> m_fired{Fired::None};
    unsigned long m_thread_id = 0;
    std::chrono::steady_clock::time_point m_start;
};

// -------------------------------------------------------------------------
// Python-side data structures for informational bindings
// -------------------------------------------------------------------------
struct PyFilamentUsage
{
    double meters;
    double grams;
    double volume_mm3;
    double cost;
};

struct PyCustomEvent
{
    double z;
    int type; // CustomGCode::Type
    int extruder;
    std::string color;
    std::string extra;
};

// -------------------------------------------------------------------------
// Export script wrapper: lightweight object passed to export()
// -------------------------------------------------------------------------
struct PyExportGCode
{
    py::list data;        // G-code lines as a Python list of strings
    std::string filename; // Suggested output filename
};

// -------------------------------------------------------------------------
// Embedded pybind11 module: "preFlight"
// -------------------------------------------------------------------------
PYBIND11_EMBEDDED_MODULE(preFlight, m)
{
    m.doc() = "preFlight G-code pre-processor API";

    // Every public binding below carries a docstring whose leading token is the Python type (attributes) or
    // signature (methods); build_stubs.py derives the preFlight.py IDE stub from these strings and fails the
    // build for any binding without one, so the stub cannot drift from the module.
    m.attr("version") = PREFLIGHT_VERSION; // stub: str  preFlight version string, e.g. "1.3.0"
    m.attr("api_version") =
        s_api_version; // stub: int  script API version this build implements; a script may declare "# preflight-api: N" and is refused when N is newer
    // Both directories are populated after interpreter init.
    m.attr("exe_dir") = ""; // stub: str  directory containing the running preFlight executable
    m.attr("user_packages_dir") =
        ""; // stub: str  per-user pip target directory added to sys.path each slice, empty when unusable

    // Raised inside a script by the watchdog when the call runs past the wall-clock limit. Derives from
    // BaseException so "except Exception" cannot swallow it.
    auto &script_timeout = py::register_exception<ScriptTimeoutError>(m, "ScriptTimeout", PyExc_BaseException);
    script_timeout.attr("__doc__") =
        "Raised in a script that ran past the preprocessing time limit; the slice then fails. Do not catch it.";
    s_script_timeout_type = script_timeout.ptr();

    // Settings wrapper: supports both gcode.settings.key and gcode.settings["key"]
    py::exec(R"PY(
class _SettingsWrapper:
    def __init__(self, d):
        object.__setattr__(self, '_d', d)
    def __getattr__(self, key):
        try:
            return self._d[key]
        except KeyError:
            raise AttributeError(key)
    def __getitem__(self, key):
        return self._d[key]
    def __contains__(self, key):
        return key in self._d
    def get(self, key, default=None):
        return self._d.get(key, default)
    def keys(self):
        return self._d.keys()
    def __len__(self):
        return len(self._d)
    def __repr__(self):
        return f"Settings({len(self._d)} keys)"
)PY",
             m.attr("__dict__"));

    // Internal logging function used by stdout/stderr redirection
    m.def("_log_stdout",
          [](const std::string &msg)
          {
              printf("%s", msg.c_str());
              fflush(stdout);
          });
    m.def("_log_stderr",
          [](const std::string &msg)
          {
              fprintf(stderr, "%s", msg.c_str());
              fflush(stderr);
          });

    // EMoveType enum
    py::enum_<EMoveType>(m, "MoveType", "Kind of G-code move")
        .value("Noop", EMoveType::Noop, "no operation")
        .value("Retract", EMoveType::Retract, "filament retraction")
        .value("Unretract", EMoveType::Unretract, "filament unretraction")
        .value("Seam", EMoveType::Seam, "seam point")
        .value("ToolChange", EMoveType::Tool_change, "tool change (T command)")
        .value("ColorChange", EMoveType::Color_change, "color change event")
        .value("PausePrint", EMoveType::Pause_Print, "pause print event")
        .value("CustomGCode", EMoveType::Custom_GCode, "custom G-code event")
        .value("Travel", EMoveType::Travel, "non-extrusion travel")
        .value("Wipe", EMoveType::Wipe, "nozzle wipe")
        .value("Extrude", EMoveType::Extrude, "extrusion move");

    // GCodeExtrusionRole enum
    py::enum_<GCodeExtrusionRole>(m, "ExtrusionRole", "Feature type of an extrusion move")
        .value("NoRole", GCodeExtrusionRole::None, "no role assigned")
        .value("Serpentine", GCodeExtrusionRole::Serpentine, "Serpentine single-path island fill")
        .value("SerpentineOverhang", GCodeExtrusionRole::SerpentineOverhang, "Serpentine fill over an unsupported area")
        .value("Perimeter", GCodeExtrusionRole::Perimeter, "inner perimeter")
        .value("ExternalPerimeter", GCodeExtrusionRole::ExternalPerimeter, "outer perimeter (visible wall)")
        .value("OverhangPerimeter", GCodeExtrusionRole::OverhangPerimeter, "perimeter over an unsupported area")
        .value("InterlockingPerimeter", GCodeExtrusionRole::InterlockingPerimeter, "interlocking boundary perimeter")
        .value("InternalInfill", GCodeExtrusionRole::InternalInfill, "sparse internal fill")
        .value("SolidInfill", GCodeExtrusionRole::SolidInfill, "solid fill that is not a top surface")
        .value("TopSolidInfill", GCodeExtrusionRole::TopSolidInfill, "top surface fill")
        .value("Ironing", GCodeExtrusionRole::Ironing, "ironing pass")
        .value("BridgeInfill", GCodeExtrusionRole::BridgeInfill, "bridging fill over gaps")
        .value("GapFill", GCodeExtrusionRole::GapFill, "thin gap fill between features")
        .value("Skirt", GCodeExtrusionRole::Skirt, "skirt or brim outline")
        .value("SupportMaterial", GCodeExtrusionRole::SupportMaterial, "support structure")
        .value("SupportMaterialInterface", GCodeExtrusionRole::SupportMaterialInterface, "support interface layer")
        .value("WipeTower", GCodeExtrusionRole::WipeTower, "wipe tower purge")
        .value("Custom", GCodeExtrusionRole::Custom, "custom G-code region, for example start or end G-code");

    // CustomGCode::Type enum
    py::enum_<CustomGCode::Type>(m, "CustomEventType", "Kind of custom G-code event placed at a print Z")
        .value("ColorChange", CustomGCode::ColorChange, "M600 color change")
        .value("PausePrint", CustomGCode::PausePrint, "M601 pause")
        .value("ToolChange", CustomGCode::ToolChange, "tool change event")
        .value("Template", CustomGCode::Template, "template custom G-code")
        .value("Custom", CustomGCode::Custom, "user custom G-code");

    // FilamentUsage data class
    py::class_<PyFilamentUsage>(m, "FilamentUsage", "Filament consumption for one extrusion role or extruder")
        .def_readonly("meters", &PyFilamentUsage::meters, "float: filament length (m)")
        .def_readonly("grams", &PyFilamentUsage::grams, "float: filament weight (g)")
        .def_readonly("volume_mm3", &PyFilamentUsage::volume_mm3, "float: filament volume (mm3)")
        .def_readonly("cost", &PyFilamentUsage::cost, "float: filament cost in the configured currency");

    // CustomEvent data class
    py::class_<PyCustomEvent>(m, "CustomEvent", "A custom G-code event (color change, pause, ...) at a print Z")
        .def_readonly("z", &PyCustomEvent::z, "float: print Z of the event (mm)")
        .def_readonly("type", &PyCustomEvent::type, "int: CustomEventType value")
        .def_readonly("extruder", &PyCustomEvent::extruder, "int: extruder index")
        .def_readonly("color", &PyCustomEvent::color, "str: color string")
        .def_readonly("extra", &PyCustomEvent::extra, "str: custom G-code text");

    // MoveVertex bindings
    using MV = GCodeProcessorResult::MoveVertex;
    py::class_<MV>(m, "Move",
                   "A single G-code movement. Positions in mm, feedrates in mm/s, temperatures in C. "
                   "Writable properties update both the move and its G-code line.")
        // Read/write properties
        .def_readwrite("feedrate", &MV::feedrate, "float: commanded feedrate (mm/s), written back as the F parameter")
        .def_readwrite("fan_speed", &MV::fan_speed,
                       "float: fan percentage (0-100); a change emits M106 before the move and the original value is "
                       "restored before the first unmodified extruding move that follows")
        .def_readwrite("temperature", &MV::temperature,
                       "float: hotend temperature (C); a change emits M104 before the move and the original value is "
                       "restored before the first unmodified extruding move that follows, unless it reads 0 (unknown)")
        .def_readwrite("delta_e", &MV::delta_extruder,
                       "float: filament displacement (mm), written back as the E parameter")
        .def_readwrite("width", &MV::width, "float: extrusion line width (mm), preview only")
        .def_readwrite("height", &MV::height, "float: extrusion height (mm), preview only")
        // Per-move annotation (overrides gcode.annotation for this specific move)
        .def_property(
            "annotation",
            [](const MV &mv) -> std::string
            {
                if (s_active_gcode)
                {
                    auto it = s_active_gcode->move_annotations.find(&mv);
                    if (it != s_active_gcode->move_annotations.end())
                        return it->second;
                }
                return "";
            },
            [](MV &mv, const std::string &val)
            {
                if (s_active_gcode)
                    s_active_gcode->move_annotations[&mv] = val;
            },
            "str: comment appended to this move's G-code line when it is modified, overrides gcode.annotation")
        // Read-only properties
        .def_readonly("type", &MV::type, "MoveType: kind of move")
        .def_readonly("role", &MV::extrusion_role, "ExtrusionRole: feature type of the extrusion")
        .def_readonly("extruder_id", &MV::extruder_id, "int: active extruder (0-based)")
        .def_readonly("color_id", &MV::cp_color_id, "int: sequential color change counter")
        .def_readonly("mm3_per_mm", &MV::mm3_per_mm, "float: volumetric rate constant (mm3 per mm of path)")
        .def_readonly("actual_feedrate", &MV::actual_feedrate, "float: feedrate after acceleration limits (mm/s)")
        .def_readonly("gcode_line_id", &MV::gcode_id, "int: 1-based line of this move in the virtual G-code file")
        .def_readonly("layer_id", &MV::layer_id, "int: index of the layer this move belongs to")
        .def_readonly("internal_only", &MV::internal_only, "bool: True for internal G2/G3 arc segments")
        // Position as individual floats
        .def_property_readonly(
            "x", [](const MV &mv) { return mv.position.x(); }, "float: X position (mm)")
        .def_property_readonly(
            "y", [](const MV &mv) { return mv.position.y(); }, "float: Y position (mm)")
        .def_property_readonly(
            "z", [](const MV &mv) { return mv.position.z(); }, "float: Z position (mm)")
        // Computed properties
        .def_property_readonly("volumetric_rate", &MV::volumetric_rate, "float: feedrate * mm3_per_mm (mm3/s)")
        .def_property_readonly("actual_volumetric_rate", &MV::actual_volumetric_rate,
                               "float: actual_feedrate * mm3_per_mm (mm3/s)")
        // Time (normal mode)
        .def_property_readonly(
            "time", [](const MV &mv) { return mv.time[0]; }, "float: move duration in seconds (normal mode)")
        .def_property_readonly(
            "time_stealth", [](const MV &mv) { return mv.time[1]; }, "float: move duration in seconds (stealth mode)")
        // Motion analysis
        .def_readonly("distance", &MV::distance, "float: XYZ path length (mm)")
        .def_readonly("junction_angle", &MV::junction_angle,
                      "float: angle from the previous move (degrees, signed: positive right, negative left)")
        .def_property_readonly(
            "acceleration", [](const MV &mv) { return mv.acceleration[0]; },
            "float: effective acceleration (mm/s2, normal mode)")
        .def_property_readonly(
            "acceleration_stealth", [](const MV &mv) { return mv.acceleration[1]; },
            "float: effective acceleration (mm/s2, stealth mode)")
        .def_property_readonly(
            "max_entry_speed", [](const MV &mv) { return mv.max_entry_speed[0]; },
            "float: junction-limited entry speed (mm/s, normal mode)")
        .def_property_readonly(
            "max_entry_speed_stealth", [](const MV &mv) { return mv.max_entry_speed[1]; },
            "float: junction-limited entry speed (mm/s, stealth mode)")
        // Fill region properties
        .def_readonly(
            "region_area", &MV::region_area,
            "float: fill region area (mm2): island for perimeters, fill surface for infill, 0 if not applicable")
        .def_property_readonly(
            "fill_pattern",
            [](const MV &mv) -> std::string
            {
                if (mv.fill_pattern < 0 || mv.fill_pattern >= static_cast<int>(ipCount))
                    return "";
                static const char *names[] = {
                    "Rectilinear",
                    "Monotonic",
                    "MonotonicLines",
                    "AlignedRectilinear",
                    "AlignedMonotonic",
                    "Grid",
                    "Triangles",
                    "Stars",
                    "Cubic",
                    "Line",
                    "Concentric",
                    "Honeycomb",
                    "3DHoneycomb",
                    "Gyroid",
                    "HilbertCurve",
                    "ArchimedeanChords",
                    "OctagramSpiral",
                    "AdaptiveCubic",
                    "SupportCubic",
                    "SupportBase",
                    "Lightning",
                    "Ensuring",
                    "ZigZag",
                };
                return names[mv.fill_pattern];
            },
            "str: infill pattern name (Rectilinear, Gyroid, ...), empty for non-fill moves");

    // Layer bindings
    py::class_<PyLayer>(m, "Layer", "All moves sharing one layer_id, with filters and layer-boundary G-code injection")
        .def_readonly("id", &PyLayer::id, "int: layer number")
        .def_readonly("z", &PyLayer::z, "float: Z height of this layer (mm)")
        .def_readonly("height", &PyLayer::height, "float: layer height (mm), delta from the previous layer")
        .def_readonly("time", &PyLayer::time, "float: total layer time in seconds (normal mode)")
        // Read-only: the list is the layer's index into the move vector, replacing it changes nothing in the G-code.
        .def_readonly("moves", &PyLayer::moves, "List[Move]: all moves in this layer")
        .def("prepend", &PyLayer::prepend, py::arg("gcode"), py::arg("comment") = "",
             "(gcode: str, comment: str = '') -> None: Insert G-code lines before the first move of this layer")
        .def("append", &PyLayer::append, py::arg("gcode"), py::arg("comment") = "",
             "(gcode: str, comment: str = '') -> None: Insert G-code lines after the last move of this layer")
        .def("moves_by_type", &PyLayer::moves_by_type, py::arg("move_type"),
             py::return_value_policy::reference_internal,
             "(move_type: MoveType) -> List[Move]: Moves of this layer with the given type")
        .def("moves_by_role", &PyLayer::moves_by_role, py::arg("role"), py::return_value_policy::reference_internal,
             "(role: ExtrusionRole) -> List[Move]: Moves of this layer with the given extrusion role")
        .def("extrusion_length", &PyLayer::extrusion_length, "() -> float: Total filament extruded in this layer (mm)")
        .def("travel_distance", &PyLayer::travel_distance,
             "() -> float: Total travel (non-extrusion) distance in this layer (mm)")
        // Raw text of the layer
        .def_readonly("first_line", &PyLayer::first_line,
                      "int: 1-based id of the first raw G-code line belonging to this layer (the line after the "
                      "previous layer's last move, so the layer-change block is included)")
        .def_readonly("last_line", &PyLayer::last_line,
                      "int: 1-based id of the last raw G-code line of this layer (its last move); text after the "
                      "final layer belongs to no layer")
        .def("find_line", &PyLayer::find_line, py::arg("text"),
             "(text: str) -> int: 1-based id of the first line of this layer containing text, 0 if none")
        .def("find_lines", &PyLayer::find_lines, py::arg("text"),
             "(text: str) -> List[int]: 1-based ids of every line of this layer containing text")
        .def("lines", &PyLayer::lines, "() -> List[Tuple[int, str]]: every raw line of this layer as (line_id, text)");

    // Top-level GCode bindings
    py::class_<PyGCode>(
        m, "GCode", "The sliced G-code handed to process(): layers, moves, statistics, settings and raw line editing")
        .def_readonly("layers", &PyGCode::layers, "List[Layer]: all layers, indexed by layer_id")
        .def_readonly("max_print_height", &PyGCode::max_print_height, "float: maximum print height (mm)")
        .def_readonly("extruder_count", &PyGCode::extruder_count, "int: number of extruders")
        .def_readonly("extruder_colors", &PyGCode::extruder_colors,
                      "List[str]: extruder colors as hex strings (#FF8000)")
        .def_readonly("spiral_vase_mode", &PyGCode::spiral_vase_mode, "bool: True if spiral vase mode is active")
        .def_readwrite("annotation", &PyGCode::annotation,
                       "str: comment appended to every modified G-code line whose move has no annotation of its own")
        .def_property_readonly("time_estimate_normal", &PyGCode::time_estimate_normal,
                               "float: total estimated print time in seconds (normal mode)")
        .def_property_readonly("time_estimate_stealth", &PyGCode::time_estimate_stealth,
                               "float: total estimated print time in seconds (stealth mode)")
        .def_property_readonly("first_layer_time", &PyGCode::first_layer_time, "float: first layer time in seconds")
        // Cost data
        .def_property_readonly(
            "filament_cost", [](PyGCode &g) -> const std::vector<float> & { return g.result->filament_cost; },
            py::return_value_policy::reference_internal, "List[float]: filament cost per extruder")
        .def_property_readonly(
            "time_cost", [](PyGCode &g) { return g.result->time_cost; }, "float: machine time cost rate")
        .def_property_readonly(
            "currency_symbol", [](PyGCode &g) { return g.result->currency_symbol; }, "str: currency symbol, e.g. $")
        // Active preset names
        .def_property_readonly(
            "preset_print", [](PyGCode &g) { return g.result->settings_ids.print; }, "str: active print profile name")
        .def_property_readonly(
            "preset_filament", [](PyGCode &g) { return g.result->settings_ids.filament; },
            "List[str]: active filament profile names")
        .def_property_readonly(
            "preset_printer", [](PyGCode &g) { return g.result->settings_ids.printer; },
            "str: active printer profile name")
        // Flat access to all live moves: the parser's seed entry, moves a previous script removed and
        // moves this script removed are all excluded. The list is cached per removal version.
        .def_property_readonly(
            "moves",
            [](PyGCode &g) -> py::object
            {
                if (g.moves_cache != nullptr && g.moves_cache_version == g.removal_version)
                    return py::reinterpret_borrow<py::object>(g.moves_cache);
                py::list live;
                for (auto &mv : g.result->moves)
                    if (g.is_live(mv))
                        live.append(py::cast(&mv, py::return_value_policy::reference));
                g.release_moves_cache();
                g.moves_cache = live.inc_ref().ptr();
                g.moves_cache_version = g.removal_version;
                return live;
            },
            "List[Move]: flat list of all live moves (removed and dead entries excluded)")
        // G-code insertion/replacement
        .def("insert", &PyGCode::insert, py::arg("line"), py::arg("gcode"), py::arg("position") = "after",
             py::arg("comment") = "",
             "(line: int, gcode: str, position: str = 'after', comment: str = '') -> None: "
             "Insert raw G-code after (default) or before the given 1-based line; comment is appended as '; comment'")
        .def("get_line", &PyGCode::get_line, py::arg("line_id"),
             "(line_id: int) -> str: Text of a raw G-code line (1-based), empty if out of range")
        .def("rewrite", &PyGCode::rewrite, py::arg("line_id"), py::arg("gcode"), py::arg("comment") = "",
             "(line_id: int, gcode: str, comment: str = '') -> None: Replace a raw G-code line (1-based)")
        .def_property_readonly("line_count", &PyGCode::line_count, "int: number of lines in the virtual G-code file")
        .def("find_line", &PyGCode::find_line, py::arg("text"), py::arg("start") = 1, py::arg("end") = 0,
             "(text: str, start: int = 1, end: int = 0) -> int: 1-based id of the first line containing text "
             "within lines start..end (end 0 = last line), 0 if none")
        .def("find_lines", &PyGCode::find_lines, py::arg("text"), py::arg("start") = 1, py::arg("end") = 0,
             "(text: str, start: int = 1, end: int = 0) -> List[int]: 1-based ids of every line containing text "
             "within lines start..end (end 0 = last line)")
        // Move query and removal
        .def("find_moves", &PyGCode::find_moves,
             "(type: Optional[MoveType] = None, role: Optional[ExtrusionRole] = None, extruder: Optional[int] = None, "
             "z_min: Optional[float] = None, z_max: Optional[float] = None) -> List[Move]: "
             "Moves matching every given filter")
        .def("remove_moves", &PyGCode::remove_moves, py::arg("predicate"),
             "(predicate: Callable[[Move], bool]) -> int: "
             "Remove every move for which predicate returns True and return the count")
        // Geometry
        .def_property_readonly(
            "z_offset", [](PyGCode &g) { return g.result->z_offset; }, "float: Z offset (mm)")
        .def_property_readonly(
            "bed_shape",
            [](PyGCode &g)
            {
                py::list shape;
                for (const auto &pt : g.result->bed_shape)
                    shape.append(py::make_tuple(pt.x(), pt.y()));
                return shape;
            },
            "List[Tuple[float, float]]: bed outline as (x, y) points")
        // All slicer settings - supports both dot and bracket access
        .def_property_readonly(
            "settings",
            [](PyGCode &g)
            {
                if (g.settings_cache != nullptr)
                    return py::reinterpret_borrow<py::object>(g.settings_cache);
                py::dict data;
                if (g.print)
                {
                    auto merge = [&data](const auto &cfg)
                    {
                        for (const auto &key : cfg.keys())
                        {
                            if (data.contains(key))
                                continue;
                            try
                            {
                                data[py::cast(key)] = py::cast(cfg.opt_serialize(key));
                            }
                            catch (...)
                            {
                            }
                        }
                    };
                    merge(g.print->config());
                    merge(g.print->default_object_config());
                    merge(g.print->default_region_config());
                }
                // Wrap in a class that supports dot notation for IDE autocomplete
                py::object wrapper = py::module_::import("preFlight").attr("_SettingsWrapper");
                py::object settings = wrapper(data);
                g.settings_cache = settings.inc_ref().ptr();
                return settings;
            },
            "Settings: every print, filament and printer setting as a string, by attribute or key")
        // Filament configuration
        .def_property_readonly(
            "filament_diameters", [](PyGCode &g) -> const std::vector<float> & { return g.result->filament_diameters; },
            py::return_value_policy::reference_internal, "List[float]: filament diameter per extruder (mm)")
        .def_property_readonly(
            "filament_densities", [](PyGCode &g) -> const std::vector<float> & { return g.result->filament_densities; },
            py::return_value_policy::reference_internal, "List[float]: filament density per extruder (g/cm3)")
        // Filament usage per role: {ExtrusionRole -> FilamentUsage}
        .def_property_readonly(
            "filament_by_role",
            [](PyGCode &g)
            {
                py::dict result;
                const auto &stats = g.result->print_statistics;
                for (const auto &[role, meters_grams] : stats.used_filaments_per_role)
                {
                    PyFilamentUsage usage;
                    usage.meters = meters_grams.first;
                    usage.grams = meters_grams.second;
                    usage.volume_mm3 = 0.0;
                    usage.cost = 0.0;
                    result[py::cast(role)] = py::cast(usage);
                }
                return result;
            },
            "Dict[ExtrusionRole, FilamentUsage]: filament usage per extrusion role")
        // Filament usage per extruder: {extruder_id -> FilamentUsage}
        .def_property_readonly(
            "filament_by_extruder",
            [](PyGCode &g)
            {
                py::dict result;
                const auto &stats = g.result->print_statistics;
                for (const auto &[ext_id, volume] : stats.volumes_per_extruder)
                {
                    PyFilamentUsage usage;
                    usage.volume_mm3 = volume;
                    usage.meters = 0.0;
                    usage.grams = 0.0;
                    // Compute meters/grams from volume if we have filament data
                    if (ext_id < g.result->filament_diameters.size())
                    {
                        float d = g.result->filament_diameters[ext_id];
                        float area = 3.14159265f * (d / 2.0f) * (d / 2.0f);
                        if (area > 0.0f)
                            usage.meters = (volume / area) / 1000.0; // mm -> m
                    }
                    if (ext_id < g.result->filament_densities.size() && g.result->filament_densities[ext_id] > 0.0f)
                    {
                        usage.grams = volume * g.result->filament_densities[ext_id] / 1000.0;
                    }
                    // Cost
                    usage.cost = 0.0;
                    auto cost_it = stats.cost_per_extruder.find(ext_id);
                    if (cost_it != stats.cost_per_extruder.end())
                        usage.cost = cost_it->second;
                    result[py::cast(static_cast<int>(ext_id))] = py::cast(usage);
                }
                return result;
            },
            "Dict[int, FilamentUsage]: filament usage per extruder")
        // Filament volumes per color change segment
        .def_property_readonly(
            "filament_by_color_change", [](PyGCode &g) -> const std::vector<double> &
            { return g.result->print_statistics.volumes_per_color_change; },
            py::return_value_policy::reference_internal, "List[float]: filament volume (mm3) per color change segment")
        // Custom G-code events (color changes, pauses, etc.)
        .def_property_readonly(
            "custom_events",
            [](PyGCode &g)
            {
                py::list events;
                for (const auto &item : g.result->custom_gcode_per_print_z)
                {
                    PyCustomEvent evt;
                    evt.z = item.print_z;
                    evt.type = static_cast<int>(item.type);
                    evt.extruder = item.extruder;
                    evt.color = item.color;
                    evt.extra = item.extra;
                    events.append(py::cast(evt));
                }
                return events;
            },
            "List[CustomEvent]: custom G-code events (color changes, pauses, ...) by print Z")
        // Performance metrics per extrusion role
        .def_property_readonly(
            "role_metrics",
            [](PyGCode &g)
            {
                py::dict result;
                for (size_t i = 0; i < static_cast<size_t>(GCodeExtrusionRole::Count); ++i)
                {
                    const auto &rm = g.result->role_metrics[i];
                    if (rm.max_commands_per_sec > 0)
                    {
                        py::dict entry;
                        entry["max_commands_per_sec"] = rm.max_commands_per_sec;
                        entry["max_layer"] = rm.max_layer;
                        result[py::cast(static_cast<GCodeExtrusionRole>(i))] = entry;
                    }
                }
                return result;
            },
            "Dict[ExtrusionRole, dict]: per-role {max_commands_per_sec, max_layer}")
        // Overall performance metrics
        .def_property_readonly(
            "overall_metrics",
            [](PyGCode &g)
            {
                py::dict result;
                result["max_commands_per_sec"] = g.result->overall_metrics.max_commands_per_sec;
                result["max_layer"] = g.result->overall_metrics.max_layer;
                return result;
            },
            "dict: {max_commands_per_sec, max_layer} over the whole print")
        // Object collision detection result
        .def_property_readonly(
            "conflict",
            [](PyGCode &g) -> py::object
            {
                if (!g.result->conflict_result.has_value())
                    return py::none();
                py::dict result;
                result["object1"] = g.result->conflict_result->_objName1;
                result["object2"] = g.result->conflict_result->_objName2;
                result["height"] = g.result->conflict_result->_height;
                result["layer"] = g.result->conflict_result->layer;
                return result;
            },
            "Optional[dict]: {object1, object2, height, layer} of the first object collision, or None");

    // Export script API (lightweight - no moves/layers/settings)
    py::class_<PyExportGCode>(m, "ExportGCode", "The final G-code handed to an Export to Script export() function")
        .def_readwrite("data", &PyExportGCode::data, "List[str]: G-code lines, each with its trailing newline")
        .def_readonly("filename", &PyExportGCode::filename,
                      "str: suggested output filename, from the output filename format");
}

// -------------------------------------------------------------------------
// Materialize all modifications into a new GCodeObject.
// Returns the new file and a mapping from old line_id (1-based) to new line_id.
// -------------------------------------------------------------------------
static void rewrite_param(std::string &gcode_line, char param, const std::string &new_value)
{
    size_t pos = std::string::npos;
    for (size_t i = 0; i < gcode_line.size(); ++i)
    {
        if (gcode_line[i] == param && (i == 0 || gcode_line[i - 1] == ' '))
        {
            pos = i;
            break;
        }
    }

    std::string token = std::string(1, param) + new_value;
    if (pos != std::string::npos)
    {
        size_t end = pos + 1;
        while (end < gcode_line.size() &&
               (isdigit(gcode_line[end]) || gcode_line[end] == '.' || gcode_line[end] == '-'))
            ++end;
        gcode_line.replace(pos, end - pos, token);
    }
    else
    {
        size_t insert_pos = gcode_line.find(';');
        if (insert_pos == std::string::npos)
            insert_pos = gcode_line.find('\n');
        if (insert_pos == std::string::npos)
            insert_pos = gcode_line.size();
        gcode_line.insert(insert_pos, " " + token);
    }
}

static void write_text(GCodeObject *output, const std::string &text)
{
    std::string t = text;
    if (!t.empty() && t.back() != '\n')
        t += '\n';
    output->append_text(t.c_str());
}

static double safe_stod(const std::string &s, double fallback = 0.0)
{
    try
    {
        return s.empty() ? fallback : std::stod(s);
    }
    catch (...)
    {
        return fallback;
    }
}

static float safe_stof(const std::string &s, float fallback = 0.0f)
{
    try
    {
        return s.empty() ? fallback : std::stof(s);
    }
    catch (...)
    {
        return fallback;
    }
}

// Value of the E parameter of a G-code line (a word starting the line or preceded by a space).
static bool read_e_param(const std::string &gcode_line, double &value)
{
    for (size_t i = 0; i < gcode_line.size(); ++i)
    {
        if (gcode_line[i] == 'E' && (i == 0 || gcode_line[i - 1] == ' '))
        {
            size_t end = i + 1;
            while (end < gcode_line.size() &&
                   (isdigit(gcode_line[end]) || gcode_line[end] == '.' || gcode_line[end] == '-'))
                ++end;
            if (end == i + 1)
                return false;
            value = safe_stod(gcode_line.substr(i + 1, end - i - 1));
            return true;
        }
        if (gcode_line[i] == ';')
            return false;
    }
    return false;
}

static GCodeObject *materialize_modifications(GCodeObject *input, const PreProcessorResult &pp_result,
                                              std::unordered_map<unsigned int, unsigned int> &id_remap)
{
    auto output = std::make_unique<GCodeObject>();

    // Fan and temperature as the printer will see them in the output stream (slicer lines and script
    // emissions both update them), plus the pending restore of a run of script overrides: a change is
    // emitted only when it differs from the output state, and the value the next unmodified extruding
    // move already carries is restored before that move, so an override ends where the script's edits
    // end instead of bleeding forward to the slicer's next M106/M104.
    float fan_out = 0.0f;
    float temp_out = 0.0f;
    bool fan_run_active = false;
    float fan_restore_to = 0.0f;
    bool temp_run_active = false;
    float temp_restore_to = 0.0f;
    const auto fan_s = [](float pct)
    {
        return std::lround(pct * 255.0f / 100.0f);
    };
    // Absolute E mode: the file's E values are running totals. e_offset is the correction applied to
    // every surviving line (script delta_e edits, and the filament of removed moves subtracted so the
    // next surviving move does not extrude it); last_abs_e is the file's E position before the offset.
    double e_offset = 0.0;
    double last_abs_e = 0.0;
    bool relative_e = false;

    for (size_t line_idx = 0; line_idx < input->line_count(); ++line_idx)
    {
        unsigned int old_line_id = static_cast<unsigned int>(line_idx + 1);
        std::string gcode_line(input->get_line_text(line_idx));

        // Skip removed lines; in absolute E mode take their filament out of the running offset
        if (pp_result.has_removals() && pp_result.removed_lines.count(old_line_id))
        {
            double removed_e = 0.0;
            if (!relative_e &&
                (GCodeReader::GCodeLine::cmd_is(gcode_line, "G1") ||
                 GCodeReader::GCodeLine::cmd_is(gcode_line, "G0")) &&
                read_e_param(gcode_line, removed_e))
            {
                e_offset -= removed_e - last_abs_e;
                last_abs_e = removed_e;
                DBG_COUNT("PP_REMOVE_E_COMPENSATED");
            }
            continue;
        }

        // Apply line replacement
        if (pp_result.has_line_replacements())
        {
            auto repl_it = pp_result.line_replacements.find(old_line_id);
            if (repl_it != pp_result.line_replacements.end())
            {
                gcode_line = repl_it->second;
                if (gcode_line.empty() || gcode_line.back() != '\n')
                    gcode_line += "\n";
            }
        }

        bool is_g0_g1 = GCodeReader::GCodeLine::cmd_is(gcode_line, "G0") ||
                        GCodeReader::GCodeLine::cmd_is(gcode_line, "G1");

        // The file's E position, read before any offset is applied to this line
        if (is_g0_g1 && !relative_e)
        {
            double line_e = 0.0;
            if (read_e_param(gcode_line, line_e))
                last_abs_e = line_e;
        }

        // Emit layer prepend before this line
        if (pp_result.has_layer_injections())
        {
            auto prep_it = pp_result.layer_prepends.find(old_line_id);
            if (prep_it != pp_result.layer_prepends.end())
                write_text(output.get(), prep_it->second);
        }

        // Emit before-insertions
        if (!pp_result.gcode_insertions_before.empty())
        {
            auto bis_it = pp_result.gcode_insertions_before.find(old_line_id);
            if (bis_it != pp_result.gcode_insertions_before.end())
                write_text(output.get(), bis_it->second);
        }

        // Apply move modifications to G0/G1 lines
        if (is_g0_g1 && pp_result.has_any_changes())
        {
            auto pp_it = pp_result.modifications.find(old_line_id);
            const MoveModification *mod_here = pp_it != pp_result.modifications.end() ? &pp_it->second : nullptr;
            const bool fan_changed_here = mod_here != nullptr && mod_here->fan_speed != mod_here->original_fan_speed;
            const bool temp_changed_here = mod_here != nullptr &&
                                           mod_here->temperature != mod_here->original_temperature;
            const bool extruding_here = pp_result.extruding_lines.count(old_line_id) != 0;

            // A run of overrides ends at the first extruding move the script left alone: put the printer
            // back to the value that move carries.
            if (fan_run_active && extruding_here && !fan_changed_here)
            {
                if (fan_s(fan_out) != fan_s(fan_restore_to))
                {
                    char fan_buf[64];
                    snprintf(fan_buf, sizeof(fan_buf), "M106 S%ld", fan_s(fan_restore_to));
                    write_text(output.get(), fan_buf);
                    fan_out = fan_restore_to;
                    DBG_COUNT("PP_FAN_RESTORE");
                }
                fan_run_active = false;
            }
            if (temp_run_active && extruding_here && !temp_changed_here)
            {
                if (temp_restore_to <= 0.0f)
                {
                    // The pre-script temperature is unknown (set by a macro the parser cannot see):
                    // commanding 0 would switch the heater off, so the override is left in place.
                    DBG_COUNT("PP_TEMP_RESTORE_UNKNOWN");
                }
                else if (std::lround(temp_out) != std::lround(temp_restore_to))
                {
                    char temp_buf[64];
                    snprintf(temp_buf, sizeof(temp_buf), "M104 S%.0f", temp_restore_to);
                    write_text(output.get(), temp_buf);
                    temp_out = temp_restore_to;
                    DBG_COUNT("PP_TEMP_RESTORE");
                }
                temp_run_active = false;
            }

            if (mod_here != nullptr)
            {
                const auto &mod = *mod_here;

                // Rewrite feedrate only if the script changed it
                if (mod.feedrate != mod.original_feedrate)
                {
                    if (mod.feedrate > 500.0f)
                        BOOST_LOG_TRIVIAL(warning)
                            << "Pre-processor: feedrate " << mod.feedrate << " mm/s (" << (mod.feedrate * 60.0f)
                            << " mm/min) on line " << old_line_id << " seems high - API uses mm/s, not mm/min";
                    char f_buf[32];
                    snprintf(f_buf, sizeof(f_buf), "%.0f", mod.feedrate * 60.0f);
                    rewrite_param(gcode_line, 'F', f_buf);
                }

                // Rewrite E parameter only if the script changed it
                if (mod.delta_e != mod.original_delta_e)
                {
                    double delta_change = static_cast<double>(mod.delta_e) - static_cast<double>(mod.original_delta_e);
                    if (relative_e)
                    {
                        char e_buf[32];
                        snprintf(e_buf, sizeof(e_buf), "%.5f", static_cast<double>(mod.delta_e));
                        rewrite_param(gcode_line, 'E', e_buf);
                    }
                    else
                    {
                        e_offset += delta_change;
                    }
                }

                // Apply cumulative E offset (absolute E mode)
                if (e_offset != 0.0 && !relative_e)
                {
                    size_t e_pos = std::string::npos;
                    for (size_t ei = 0; ei < gcode_line.size(); ++ei)
                    {
                        if (gcode_line[ei] == 'E' && (ei == 0 || gcode_line[ei - 1] == ' '))
                        {
                            e_pos = ei;
                            break;
                        }
                    }
                    if (e_pos != std::string::npos)
                    {
                        size_t e_val_start = e_pos + 1;
                        size_t e_val_end = e_val_start;
                        while (e_val_end < gcode_line.size() &&
                               (isdigit(gcode_line[e_val_end]) || gcode_line[e_val_end] == '.' ||
                                gcode_line[e_val_end] == '-'))
                            ++e_val_end;
                        double original_e = safe_stod(gcode_line.substr(e_val_start, e_val_end - e_val_start));
                        char e_buf[32];
                        snprintf(e_buf, sizeof(e_buf), "%.5f", original_e + e_offset);
                        rewrite_param(gcode_line, 'E', e_buf);
                    }
                }

                // Fan override: M106 only when the output state changes; remember what to restore
                if (fan_changed_here)
                {
                    if (fan_s(mod.fan_speed) != fan_s(fan_out))
                    {
                        char fan_buf[64];
                        snprintf(fan_buf, sizeof(fan_buf), "M106 S%ld", fan_s(mod.fan_speed));
                        write_text(output.get(), fan_buf);
                        fan_out = mod.fan_speed;
                        DBG_COUNT("PP_M106_EMIT");
                    }
                    else
                        DBG_COUNT("PP_M106_COLLAPSED");
                    fan_run_active = true;
                    fan_restore_to = mod.original_fan_speed;
                }

                // Temperature override, same shape
                if (temp_changed_here)
                {
                    if (mod.temperature > 500.0f)
                        BOOST_LOG_TRIVIAL(warning) << "Pre-processor: temperature " << mod.temperature << "C on line "
                                                   << old_line_id << " exceeds 500C - verify this is intentional";
                    if (std::lround(mod.temperature) != std::lround(temp_out))
                    {
                        char temp_buf[64];
                        snprintf(temp_buf, sizeof(temp_buf), "M104 S%.0f", mod.temperature);
                        write_text(output.get(), temp_buf);
                        temp_out = mod.temperature;
                        DBG_COUNT("PP_M104_EMIT");
                    }
                    else
                        DBG_COUNT("PP_M104_COLLAPSED");
                    temp_run_active = true;
                    temp_restore_to = mod.original_temperature;
                }

                // Add annotation comment
                if (!mod.annotation.empty())
                {
                    if (!gcode_line.empty() && gcode_line.back() == '\n')
                        gcode_line.pop_back();
                    gcode_line += " ; " + mod.annotation + "\n";
                }
            }
            else if (e_offset != 0.0 && !relative_e)
            {
                // Unmodified move but cumulative E offset needs applying
                size_t e_pos = std::string::npos;
                for (size_t ei = 0; ei < gcode_line.size(); ++ei)
                {
                    if (gcode_line[ei] == 'E' && (ei == 0 || gcode_line[ei - 1] == ' '))
                    {
                        e_pos = ei;
                        break;
                    }
                }
                if (e_pos != std::string::npos)
                {
                    size_t e_val_start = e_pos + 1;
                    size_t e_val_end = e_val_start;
                    while (e_val_end < gcode_line.size() &&
                           (isdigit(gcode_line[e_val_end]) || gcode_line[e_val_end] == '.' ||
                            gcode_line[e_val_end] == '-'))
                        ++e_val_end;
                    double original_e = safe_stod(gcode_line.substr(e_val_start, e_val_end - e_val_start));
                    char e_buf[32];
                    snprintf(e_buf, sizeof(e_buf), "%.5f", original_e + e_offset);
                    rewrite_param(gcode_line, 'E', e_buf);
                }
            }
        }

        // Track fan/temp/E-mode state from non-move commands
        if (!is_g0_g1 && pp_result.has_any_changes())
        {
            // A slicer-emitted state change mid-run becomes the new prevailing value: it is what the
            // printer sees and what the run restores to.
            if (GCodeReader::GCodeLine::cmd_is(gcode_line, "M106"))
            {
                size_t s_pos = gcode_line.find('S');
                if (s_pos != std::string::npos)
                {
                    fan_out = safe_stof(gcode_line.substr(s_pos + 1)) * 100.0f / 255.0f;
                    if (fan_run_active)
                        fan_restore_to = fan_out;
                }
            }
            else if (GCodeReader::GCodeLine::cmd_is(gcode_line, "M107"))
            {
                fan_out = 0.0f;
                if (fan_run_active)
                    fan_restore_to = 0.0f;
            }
            else if (GCodeReader::GCodeLine::cmd_is(gcode_line, "M104"))
            {
                size_t s_pos = gcode_line.find('S');
                if (s_pos != std::string::npos)
                {
                    temp_out = safe_stof(gcode_line.substr(s_pos + 1));
                    if (temp_run_active)
                        temp_restore_to = temp_out;
                }
            }
            else if (GCodeReader::GCodeLine::cmd_is(gcode_line, "M83"))
            {
                relative_e = true;
            }
            else if (GCodeReader::GCodeLine::cmd_is(gcode_line, "M82"))
            {
                relative_e = false;
            }
            else if (GCodeReader::GCodeLine::cmd_is(gcode_line, "G92"))
            {
                double reset_e = 0.0;
                if (read_e_param(gcode_line, reset_e))
                {
                    e_offset = 0.0;
                    last_abs_e = reset_e;
                }
            }
        }

        // Record the line ID mapping: old_line_id -> new_line_id (1-based)
        unsigned int new_line_id = static_cast<unsigned int>(output->line_count() + 1);
        id_remap[old_line_id] = new_line_id;

        write_text(output.get(), gcode_line);

        // Emit layer append after this line
        if (pp_result.has_layer_injections())
        {
            auto app_it = pp_result.layer_appends.find(old_line_id);
            if (app_it != pp_result.layer_appends.end())
                write_text(output.get(), app_it->second);
        }

        // Emit after-insertions
        if (!pp_result.gcode_insertions.empty())
        {
            auto ins_it = pp_result.gcode_insertions.find(old_line_id);
            if (ins_it != pp_result.gcode_insertions.end())
                write_text(output.get(), ins_it->second);
        }
    }

    return output.release();
}

// -------------------------------------------------------------------------
// Build a PreProcessorResult from snapshot comparison and script state
// -------------------------------------------------------------------------
struct MoveSnapshot
{
    float feedrate;
    float fan_speed;
    float temperature;
    float delta_e;
};

static PreProcessorResult collect_script_result(GCodeProcessorResult &result,
                                                const std::vector<MoveSnapshot> &snapshots,
                                                const std::string &annotation, PyGCode &gcode)
{
    PreProcessorResult pp_result;

    // Compare all moves against snapshot
    for (size_t i = 0; i < result.moves.size(); ++i)
    {
        const auto &mv = result.moves[i];
        const auto &snap = snapshots[i];

        if (mv.feedrate != snap.feedrate || mv.fan_speed != snap.fan_speed || mv.temperature != snap.temperature ||
            mv.delta_extruder != snap.delta_e)
        {
            // Per-move annotation takes priority over the global annotation
            std::string move_annotation = annotation;
            auto it = gcode.move_annotations.find(&mv);
            if (it != gcode.move_annotations.end() && !it->second.empty())
                move_annotation = it->second;

            // Segments of one G-code line (arc interpolation) share its id: the last edit wins, counted
            if (pp_result.modifications.count(mv.gcode_id))
                DBG_COUNT("PP_SHARED_ID_EDIT");
            pp_result.modifications[mv.gcode_id] = {mv.feedrate,       mv.fan_speed,  mv.temperature,
                                                    mv.delta_extruder, snap.feedrate, snap.fan_speed,
                                                    snap.temperature,  snap.delta_e,  move_annotation};
        }
    }

    // Fan and temperature overrides are restored at the first unmodified extruding move after a run,
    // so the materializer needs to know which lines extrude; only built when such an override exists.
    bool needs_restore_boundary = false;
    for (const auto &[line_id, mod] : pp_result.modifications)
        if (mod.fan_speed != mod.original_fan_speed || mod.temperature != mod.original_temperature)
        {
            needs_restore_boundary = true;
            break;
        }
    if (needs_restore_boundary)
        for (const auto &mv : result.moves)
            if (mv.gcode_id != 0 && mv.type == EMoveType::Extrude)
                pp_result.extruding_lines.insert(mv.gcode_id);

    pp_result.gcode_insertions = std::move(gcode.insertions_after);
    pp_result.gcode_insertions_before = std::move(gcode.insertions_before);
    pp_result.line_replacements = std::move(gcode.line_replacements);
    pp_result.removed_lines = std::move(gcode.removed_move_ids);

    for (auto &layer : gcode.layers)
    {
        if (!layer.prepend_gcode.empty() && !layer.moves.empty())
            pp_result.layer_prepends[layer.moves.front()->gcode_id] = layer.prepend_gcode;
        if (!layer.append_gcode.empty() && !layer.moves.empty())
            pp_result.layer_appends[layer.moves.back()->gcode_id] = layer.append_gcode;
        layer.prepend_gcode.clear();
        layer.append_gcode.clear();
    }

    return pp_result;
}

// -------------------------------------------------------------------------
// Shared Python interpreter initialization (used by both preprocessing and export)
// -------------------------------------------------------------------------
static bool s_python_interpreter_ok = false;
static bool s_python_setup_ok = false;
static std::once_flag s_python_init_flag;
static std::once_flag s_python_setup_flag;
static std::once_flag s_python_gil_release_flag;
static PyThreadState *s_main_tstate = nullptr;

static void ensure_python_initialized()
{
    // Two-phase init: (1) interpreter startup, (2) post-init setup.
    // Separated so the Py_IsInitialized() guard on retry doesn't skip setup.
    std::call_once(s_python_init_flag,
                   []()
                   {
                       auto exe_dir = boost::dll::program_location().parent_path();
#ifdef _WIN32
                       auto home_path = exe_dir / "python";
#elif defined(__APPLE__)
                       auto home_path = exe_dir / ".." / "python";
#else
                       auto home_path = exe_dir / ".." / "python";
#endif
                       if (boost::filesystem::exists(home_path) && !Py_IsInitialized())
                       {
                           auto home_w = home_path.wstring();
                           auto exe_w = boost::dll::program_location().wstring();
                           auto ver_str = std::to_wstring(PY_MAJOR_VERSION) + std::to_wstring(PY_MINOR_VERSION);

                           PyConfig config;
                           PyConfig_InitPythonConfig(&config);
                           config.install_signal_handlers = 0;
                           config.module_search_paths_set = 1;
#ifndef _WIN32
                           // Ignore a user's system-Python env vars (PYTHONHOME/PYTHONPATH); Windows is
                           // already isolated by the embeddable ._pth.
                           config.use_environment = 0;
#endif

                           PyStatus status;
                           bool config_ok = true;

#ifdef _WIN32
                           auto zip_path = (home_path / (L"python" + ver_str + L".zip")).wstring();
                           status = PyWideStringList_Append(&config.module_search_paths, zip_path.c_str());
                           config_ok = config_ok && !PyStatus_Exception(status);
                           status = PyWideStringList_Append(&config.module_search_paths, home_w.c_str());
                           config_ok = config_ok && !PyStatus_Exception(status);
#else
                           auto lib_ver = std::string("python") + std::to_string(PY_MAJOR_VERSION) + "." +
                                          std::to_string(PY_MINOR_VERSION);
                           auto stdlib_path = (home_path / "lib" / lib_ver).wstring();
                           auto dynload_path = (home_path / "lib" / lib_ver / "lib-dynload").wstring();
                           status = PyWideStringList_Append(&config.module_search_paths, stdlib_path.c_str());
                           config_ok = config_ok && !PyStatus_Exception(status);
                           status = PyWideStringList_Append(&config.module_search_paths, dynload_path.c_str());
                           config_ok = config_ok && !PyStatus_Exception(status);
                           status = PyWideStringList_Append(&config.module_search_paths, home_w.c_str());
                           config_ok = config_ok && !PyStatus_Exception(status);
#endif
#ifdef _WIN32
                           auto site_packages = (home_path / "Lib" / "site-packages").wstring();
#else
                           auto site_packages = (home_path / "lib" / lib_ver / "site-packages").wstring();
#endif
                           status = PyWideStringList_Append(&config.module_search_paths, site_packages.c_str());
                           config_ok = config_ok && !PyStatus_Exception(status);

                           status = PyConfig_SetString(&config, &config.home, home_w.c_str());
                           config_ok = config_ok && !PyStatus_Exception(status);
                           status = PyConfig_SetString(&config, &config.program_name, exe_w.c_str());
                           config_ok = config_ok && !PyStatus_Exception(status);

                           if (!config_ok)
                           {
                               BOOST_LOG_TRIVIAL(error)
                                   << "Python: PyConfig setup failed, falling back to system Python";
                               PyConfig_Clear(&config);
                               py::initialize_interpreter();
                           }
                           else
                           {
                               status = Py_InitializeFromConfig(&config);
                               PyConfig_Clear(&config);
                               if (PyStatus_Exception(status))
                               {
                                   BOOST_LOG_TRIVIAL(error)
                                       << "Python: Py_InitializeFromConfig failed, falling back to system Python";
                                   if (!Py_IsInitialized())
                                       py::initialize_interpreter();
                               }
                           }

                           const char *runtime_ver = Py_GetVersion();
                           if (runtime_ver)
                           {
                               int rt_major = 0, rt_minor = 0;
                               if (sscanf(runtime_ver, "%d.%d", &rt_major, &rt_minor) == 2)
                               {
                                   if (rt_major != PY_MAJOR_VERSION || rt_minor != PY_MINOR_VERSION)
                                       BOOST_LOG_TRIVIAL(error)
                                           << "Python: version mismatch! Built against " << PY_MAJOR_VERSION << "."
                                           << PY_MINOR_VERSION << " but runtime is " << runtime_ver;
                               }
                           }

                           BOOST_LOG_TRIVIAL(info) << "Python: using bundled runtime at " << home_path << " (runtime "
                                                   << (runtime_ver ? runtime_ver : "unknown") << ")";
                       }
                       else
                       {
                           if (!Py_IsInitialized())
                               py::initialize_interpreter();
                       }

                       s_python_interpreter_ok = true;
                   });

    if (!s_python_interpreter_ok)
        throw Luminary::RuntimeError("Python interpreter initialization failed");

    std::call_once(s_python_setup_flag,
                   [&]()
                   {
                       auto exe_dir = boost::dll::program_location().parent_path();

                       py::module_::import("preFlight").attr("exe_dir") = exe_dir.string();

                       // User packages install outside the app (data_dir/python-packages/<pyver>) so they
                       // survive upgrades. Refuse a group/other-writable dir - it would be a code-injection
                       // path into this process. Actual sys.path wiring happens per-slice below.
                       {
                           namespace fs = boost::filesystem;
                           std::string pkg_dir = user_python_packages_dir();
                           bool pkg_dir_ok = false;
                           try
                           {
                               fs::create_directories(pkg_dir);
#ifndef _WIN32
                               // Own the dir tightly (0700). User-private-group distros (umask 002)
                               // create it group-writable - not an exposure there, but normalize so it
                               // cannot become a code-injection path, then reject only a genuine others-write.
                               boost::system::error_code perm_ec;
                               fs::permissions(pkg_dir, fs::owner_all, perm_ec);
                               fs::perms p = fs::status(pkg_dir).permissions();
                               if ((p & fs::others_write) != fs::no_perms)
                                   BOOST_LOG_TRIVIAL(error)
                                       << "Pre-processor: package dir writable by others, not added to path: "
                                       << pkg_dir;
                               else
                                   pkg_dir_ok = true;
#else
                               pkg_dir_ok = true;
#endif
                           }
                           catch (const std::exception &e)
                           {
                               BOOST_LOG_TRIVIAL(error)
                                   << "Pre-processor: could not prepare package dir " << pkg_dir << ": " << e.what();
                           }
                           py::module_::import("preFlight").attr("user_packages_dir") = pkg_dir_ok ? pkg_dir
                                                                                                   : std::string();
                       }

                       py::exec(R"(
import sys, os, preFlight

class _StdoutRedirect:
    def write(self, text):
        if text:
            preFlight._log_stdout(text)
    def flush(self):
        pass

class _StderrRedirect:
    def write(self, text):
        if text:
            preFlight._log_stderr(text)
    def flush(self):
        pass

sys.stdout = _StdoutRedirect()
sys.stderr = _StderrRedirect()
sys.dont_write_bytecode = True
)");
                       s_python_setup_ok = true;
                   });

    if (!s_python_setup_ok)
        throw Luminary::RuntimeError("Python post-init setup failed");

    // Release the GIL exactly once after init so any background-slicing worker
    // thread can acquire it. Otherwise the init thread owns the GIL for the
    // process lifetime and other threads' Python calls crash (or deadlock).
    std::call_once(s_python_gil_release_flag, []() { s_main_tstate = PyEval_SaveThread(); });
}

// -------------------------------------------------------------------------
// Main entry point: run all scripts with per-script materialization
// -------------------------------------------------------------------------
PreProcessorResult run_pre_processor_scripts(GCodeProcessorResult &result, GCodeObject *&gcode_object,
                                             const std::vector<std::string> &script_paths,
                                             const std::string &resources_dir, const Print *print,
                                             double timeout_seconds)
{
    PreProcessorResult pp_result;
    if (script_paths.empty())
        return pp_result;

    PyGCode gcode;
    gcode.result = &result;
    gcode.gcode_object = gcode_object;
    gcode.print = print;
    gcode.build_layers();

    ScriptWatchdog watchdog(print != nullptr ? std::function<bool()>([print]() { return print->canceled(); })
                                             : std::function<bool()>(),
                            timeout_seconds);

    try
    {
        ensure_python_initialized();

        // Background slicing runs on a worker thread that does not own the GIL.
        py::gil_scoped_acquire gil;

        // Add user-installed packages (data_dir/python-packages/<pyver>) to sys.path each slice so
        // packages installed via the Python Console appear without an app restart. addsitedir appends
        // after stdlib (no shadowing) and processes .pth files; it is idempotent across slices.
        try
        {
            py::object pkg_dir = py::module_::import("preFlight").attr("user_packages_dir");
            if (!pkg_dir.cast<std::string>().empty())
                py::module_::import("site").attr("addsitedir")(pkg_dir);
        }
        catch (const std::exception &e)
        {
            BOOST_LOG_TRIVIAL(warning) << "Pre-processor: addsitedir failed: " << e.what();
        }

        // The SIGINT handler can only be touched from the interpreter's main thread; the watchdog does
        // not depend on it (it raises into the executing thread directly).
        bool is_main_thread = false;
        try
        {
            py::module_ threading_mod = py::module_::import("threading");
            is_main_thread = threading_mod.attr("current_thread")().is(threading_mod.attr("main_thread")());
        }
        catch (...)
        {
        }
        if (timeout_seconds > 0.0)
            BOOST_LOG_TRIVIAL(info) << "Pre-processor: script time limit " << timeout_seconds << " s";

        // =====================================================================
        // Per-slice state isolation: snapshot mutable Python state before the
        // script loop and restore it after, regardless of exceptions.
        //
        // This is best-effort, NOT hermetic isolation. Known UNCLEANED state:
        //   - builtins namespace mutations (builtins.print = ...)
        //   - monkey-patches to pre-existing modules (math.MY_CONST = ...)
        //   - atexit handlers (accumulate but never fire)
        //   - logging handler registrations
        //   - C extension internal state
        //   - gc.callbacks, weakref callbacks
        //   - locale/codec/decimal context changes
        // Full isolation would require process boundaries or sub-interpreters,
        // neither of which is viable with pybind11 embedded modules.
        // =====================================================================

        namespace fs = boost::filesystem;

        py::module_ sys_mod = py::module_::import("sys");
        py::module_ os_mod = py::module_::import("os");
        py::module_ signal_mod = py::module_::import("signal");

        // Snapshot sys.path (copy the list, not a reference)
        py::list saved_path = py::list(sys_mod.attr("path"));

        // Snapshot sys.modules keys (to detect script-added modules)
        py::dict modules = sys_mod.attr("modules");
        std::unordered_set<std::string> saved_module_keys;
        for (auto item : modules)
            saved_module_keys.insert(py::str(item.first).cast<std::string>());

        // Snapshot mutable interpreter settings
        py::object saved_dont_write_bytecode = sys_mod.attr("dont_write_bytecode");
        py::object saved_excepthook = sys_mod.attr("excepthook");
        // SIGINT handling is only valid on the main thread; skip otherwise.
        py::object saved_sigint_handler = is_main_thread ? signal_mod.attr("getsignal")(signal_mod.attr("SIGINT"))
                                                         : py::object(py::none());

        // Scripts run with the executable directory as their working directory. os.chdir is
        // process-global, so the host's own working directory is saved here and put back after the
        // scripts: the console resolves relative output paths against it.
        auto exe_dir = boost::dll::program_location().parent_path();
        py::object saved_cwd = os_mod.attr("getcwd")();
        os_mod.attr("chdir")(exe_dir.string());

        // Collect script directories for the whitelist module cleanup
        std::unordered_set<std::string> script_dirs;
        for (const auto &sp : script_paths)
            script_dirs.insert(fs::path(sp).parent_path().string());

        // --- Script execution loop ---
        for (const auto &script_path : script_paths)
        {
            if (print && print->canceled())
            {
                BOOST_LOG_TRIVIAL(info) << "Pre-processor: canceled by user";
                break;
            }

            std::string script_name = fs::path(script_path).filename().string();

            try
            {
                BOOST_LOG_TRIVIAL(info) << "Pre-processor: running " << script_name;

                // Snapshot before this script
                std::vector<MoveSnapshot> snapshots;
                snapshots.reserve(result.moves.size());
                for (const auto &mv : result.moves)
                    snapshots.push_back({mv.feedrate, mv.fan_speed, mv.temperature, mv.delta_extruder});

                const std::string module_stem = fs::path(script_path).stem().string();
                if (s_reserved_module_names.count(module_stem))
                {
                    std::string err = "Preprocessing script '" + script_name +
                                      "' cannot be used: its name shadows Python's built-in '" + module_stem +
                                      "' module. Rename the script file.";
                    BOOST_LOG_TRIVIAL(error) << err;
                    pp_result.errors.push_back(err);
                    DBG_COUNT("PP_SCRIPT_REJECTED");
                    continue;
                }
                if (const int wanted = declared_api_version(script_path); wanted > s_api_version)
                {
                    std::string err = "Preprocessing script '" + script_name + "' declares preflight-api " +
                                      std::to_string(wanted) + " but this preFlight provides API version " +
                                      std::to_string(s_api_version) + ". Update preFlight or the script.";
                    BOOST_LOG_TRIVIAL(error) << err;
                    pp_result.errors.push_back(err);
                    DBG_COUNT("PP_SCRIPT_REJECTED");
                    continue;
                }

                // The script directory goes to the END of sys.path: the script and its sibling helper
                // modules resolve, but a sibling named like a standard-library module cannot shadow it.
                py::list path = sys_mod.attr("path");
                std::string script_dir = fs::path(script_path).parent_path().string();
                path.attr("append")(script_dir);

                py::module_ script_mod = py::module_::import(module_stem.c_str());

                if (py::hasattr(script_mod, "process"))
                {
                    gcode.annotation.clear();
                    gcode.move_annotations.clear();
                    s_active_gcode = &gcode;
                    watchdog.arm();
                    ScriptWatchdog::Fired fired = ScriptWatchdog::Fired::None;
                    try
                    {
                        script_mod.attr("process")(py::cast(&gcode, py::return_value_policy::reference));
                        fired = watchdog.disarm();
                    }
                    catch (...)
                    {
                        watchdog.disarm();
                        throw;
                    }
                    s_active_gcode = nullptr;
                    if (fired == ScriptWatchdog::Fired::Timeout)
                    {
                        // The call returned just as the limit hit: the limit still applies.
                        DBG_COUNT("PP_SCRIPT_TIMEOUT");
                        pp_result.timed_out = true;
                        pp_result.timed_out_script = script_name;
                        break;
                    }
                    if (fired == ScriptWatchdog::Fired::Cancel)
                        break;
                    pp_result.scripts_executed++;

                    std::string annotation = gcode.annotation;

                    BOOST_LOG_TRIVIAL(info) << "Pre-processor: " << script_name << " completed";

                    // Collect this script's modifications
                    PreProcessorResult script_result = collect_script_result(result, snapshots, annotation, gcode);

                    // Materialize if this script changed anything
                    if (script_result.has_any_changes())
                    {
                        std::unordered_map<unsigned int, unsigned int> id_remap;
                        GCodeObject *new_gco = materialize_modifications(gcode.gcode_object, script_result, id_remap);

                        // Remap gcode_id on all MoveVertex entries; zero out removed moves
                        for (auto &mv : result.moves)
                        {
                            auto it = id_remap.find(mv.gcode_id);
                            if (it != id_remap.end())
                                mv.gcode_id = it->second;
                            else
                                mv.gcode_id = 0;
                        }

                        // Don't delete the old GCodeObject here - the caller manages ownership.
                        // For multi-script chains, delete the intermediate (non-original) object.
                        if (gcode.gcode_object != gcode_object)
                            delete gcode.gcode_object;
                        gcode.gcode_object = new_gco;

                        // Rebuild layers with fresh gcode_ids
                        gcode.build_layers();

                        BOOST_LOG_TRIVIAL(info)
                            << "Pre-processor: materialized " << script_name << " ("
                            << script_result.modifications.size() << " moves, "
                            << script_result.gcode_insertions.size() + script_result.gcode_insertions_before.size()
                            << " insertions)";
                    }
                }
                else
                {
                    BOOST_LOG_TRIVIAL(warning)
                        << "Pre-processor: " << script_name << " has no process() function, skipping";
                }

                // Per-script: remove the script module so edits are picked up on re-slice.
                // (sys.path restore happens at the end of the entire loop, not per-script.)
                std::string module_name = fs::path(script_path).stem().string();
                if (modules.contains(module_name))
                    modules.attr("pop")(module_name);
            }
            catch (const py::error_already_set &e)
            {
                s_active_gcode = nullptr;
                if (s_script_timeout_type != nullptr && e.matches(s_script_timeout_type))
                {
                    DBG_COUNT("PP_SCRIPT_TIMEOUT");
                    BOOST_LOG_TRIVIAL(error) << "Pre-processor: script " << script_name << " exceeded the time limit";
                    pp_result.timed_out = true;
                    pp_result.timed_out_script = script_name;
                    break;
                }
                if (e.matches(PyExc_KeyboardInterrupt))
                {
                    DBG_COUNT("PP_SCRIPT_CANCEL_INTERRUPT");
                    BOOST_LOG_TRIVIAL(info) << "Pre-processor: script interrupted by user";
                    break;
                }
                std::string err = "Preprocessing script '" + script_name + "' failed: " + e.what();
                BOOST_LOG_TRIVIAL(error) << err;
                pp_result.errors.push_back(err);

                // Per-script module cleanup even on failure
                std::string module_name = fs::path(script_path).stem().string();
                if (modules.contains(module_name))
                    modules.attr("pop")(module_name);
            }
        }

        // =====================================================================
        // Post-slice state restoration (always runs, even after exceptions)
        // =====================================================================

        // Restore sys.path to pre-script state
        sys_mod.attr("path") = saved_path;

        // Whitelisted module cleanup: only remove modules whose __file__ is
        // under a script directory. This avoids invalidating pybind11's type
        // registry cache (which holds pointers to type objects in sys.modules).
        py::dict current_modules = sys_mod.attr("modules");
        std::vector<std::string> modules_to_remove;
        for (auto item : current_modules)
        {
            std::string key = py::str(item.first).cast<std::string>();
            if (saved_module_keys.count(key))
                continue; // was present before scripts ran
            // Check if the module's __file__ is under a script directory
            py::handle mod = item.second;
            bool should_remove = false;
            if (py::hasattr(mod, "__file__") && !mod.attr("__file__").is_none())
            {
                std::string mod_file = py::str(mod.attr("__file__")).cast<std::string>();
                std::string mod_dir = fs::path(mod_file).parent_path().string();
                if (script_dirs.count(mod_dir))
                    should_remove = true;
            }
            else
            {
                // No __file__ (built-in or namespace package) - leave it
            }
            if (should_remove)
                modules_to_remove.push_back(key);
        }
        for (const auto &key : modules_to_remove)
            current_modules.attr("pop")(key);

        // Restore interpreter settings
        sys_mod.attr("dont_write_bytecode") = saved_dont_write_bytecode;
        sys_mod.attr("excepthook") = saved_excepthook;
        if (is_main_thread)
            signal_mod.attr("signal")(signal_mod.attr("SIGINT"), saved_sigint_handler);

        // Restore the host's working directory (scripts may also have changed it)
        os_mod.attr("chdir")(saved_cwd);

        // Warn about lingering threads: preFlight spawns no Python threads, so every thread other than
        // the interpreter's main thread was started by a script and still holds references into
        // storage the next slice replaces.
        try
        {
            py::module_ threading = py::module_::import("threading");
            py::list threads = threading.attr("enumerate")();
            py::object main_thread = threading.attr("main_thread")();
            int lingering = 0;
            for (auto t : threads)
                if (!t.is(main_thread))
                    lingering++;
            if (lingering > 0)
            {
                DBG_COUNT_ADD("PP_LINGERING_THREADS", lingering);
                BOOST_LOG_TRIVIAL(warning) << "Pre-processor: " << lingering
                                           << " script thread(s) still running after scripts finished. "
                                              "Scripts must not leave threads behind.";
            }
        }
        catch (...)
        {
            // threading module unavailable or enumerate failed - not critical
        }

        gcode.release_moves_cache();
        watchdog.stop();

        // Update the caller's GCodeObject pointer if we materialized
        if (gcode.gcode_object != gcode_object)
        {
            gcode_object = gcode.gcode_object;
        }
    }
    catch (const std::exception &e)
    {
        watchdog.stop();
        std::string err = std::string("Preprocessing initialization error: ") + e.what();
        BOOST_LOG_TRIVIAL(error) << err;
        pp_result.errors.push_back(err);
    }

    // All modifications are already baked into the virtual file.
    // Return empty result so pass 2 skips PP modification logic.
    return pp_result;
}

// -------------------------------------------------------------------------
// Export to Script: run a user script with the raw G-code data
ExportScriptResult run_export_script(const std::string &script_path, const std::string &gcode_buffer,
                                     const std::string &filename, std::function<bool()> canceled,
                                     double timeout_seconds)
{
    ExportScriptResult result;
    namespace fs = boost::filesystem;
    ScriptWatchdog watchdog(std::move(canceled), timeout_seconds);

    try
    {
        ensure_python_initialized();

        // Background slicing runs on a worker thread that does not own the GIL.
        py::gil_scoped_acquire gil;

        // Export scripts see the same user-installed packages as preprocessing (see addsitedir note above).
        try
        {
            py::object pkg_dir = py::module_::import("preFlight").attr("user_packages_dir");
            if (!pkg_dir.cast<std::string>().empty())
                py::module_::import("site").attr("addsitedir")(pkg_dir);
        }
        catch (const std::exception &e)
        {
            BOOST_LOG_TRIVIAL(warning) << "Export: addsitedir failed: " << e.what();
        }

        std::string script_name = fs::path(script_path).filename().string();
        std::string script_dir = fs::path(script_path).parent_path().string();
        std::string module_name = fs::path(script_path).stem().string();

        // Reject scripts whose filename shadows a standard library module
        if (s_reserved_module_names.count(module_name))
        {
            result.error_message = "Export script '" + script_name +
                                   "' cannot be used because its name conflicts with Python's built-in '" +
                                   module_name + "' module. Please rename the script file.";
            BOOST_LOG_TRIVIAL(error) << result.error_message;
            return result;
        }

        // Split the G-code buffer into lines (preserving trailing newlines)
        PyExportGCode gcode;
        gcode.filename = filename;

        {
            size_t pos = 0;
            while (pos < gcode_buffer.size())
            {
                size_t nl = gcode_buffer.find('\n', pos);
                if (nl == std::string::npos)
                {
                    gcode.data.append(py::str(gcode_buffer.substr(pos)));
                    break;
                }
                gcode.data.append(py::str(gcode_buffer.substr(pos, nl - pos + 1)));
                pos = nl + 1;
            }
        }

        BOOST_LOG_TRIVIAL(info) << "Export script: running " << script_name;

        // Snapshot sys.path and sys.modules for restoration after the script
        py::module_ sys_mod = py::module_::import("sys");
        py::list sys_path = sys_mod.attr("path");
        py::list path_snapshot(sys_path);
        py::dict sys_modules = sys_mod.attr("modules");
        py::list modules_snapshot(sys_modules.attr("keys")());

        // Add script directory to sys.path
        sys_path.attr("insert")(0, script_dir);

        try
        {
            py::module_ script_mod = py::module_::import(module_name.c_str());

            if (py::hasattr(script_mod, "export"))
            {
                watchdog.arm();
                ScriptWatchdog::Fired fired = ScriptWatchdog::Fired::None;
                try
                {
                    script_mod.attr("export")(py::cast(&gcode, py::return_value_policy::reference));
                    fired = watchdog.disarm();
                }
                catch (...)
                {
                    watchdog.disarm();
                    throw;
                }
                if (fired == ScriptWatchdog::Fired::Timeout)
                {
                    DBG_COUNT("PP_EXPORT_SCRIPT_TIMEOUT");
                    result.timed_out = true;
                    result.error_message = "Export script '" + script_name + "' exceeded the time limit";
                }
                else if (fired == ScriptWatchdog::Fired::Cancel)
                    result.error_message = "Export script interrupted by user";
                else
                {
                    BOOST_LOG_TRIVIAL(info) << "Export script: " << script_name << " completed successfully";
                    result.success = true;
                }
            }
            else
            {
                result.error_message = "Export script '" + script_name + "' has no export() function";
                BOOST_LOG_TRIVIAL(error) << result.error_message;
            }
        }
        catch (const py::error_already_set &e)
        {
            if (s_script_timeout_type != nullptr && e.matches(s_script_timeout_type))
            {
                DBG_COUNT("PP_EXPORT_SCRIPT_TIMEOUT");
                result.timed_out = true;
                result.error_message = "Export script '" + script_name + "' exceeded the time limit";
            }
            else if (e.matches(PyExc_SystemExit))
            {
                result.error_message =
                    "Export script '" + script_name +
                    "' called sys.exit(). Use 'raise RuntimeError(...)' instead of sys.exit() in export scripts.";
            }
            else if (e.matches(PyExc_KeyboardInterrupt))
            {
                result.error_message = "Export script interrupted by user";
            }
            else
            {
                result.error_message = "Export script '" + script_name + "' failed:\n" + std::string(e.what());
            }
            BOOST_LOG_TRIVIAL(error) << result.error_message;
        }

        // Restore sys.path from snapshot (always runs, even after exceptions)
        sys_mod.attr("path") = path_snapshot;

        // Two-phase module cleanup: collect keys to remove, then pop them.
        // Cannot pop during iteration - CPython raises RuntimeError on dict mutation.
        std::vector<std::string> modules_to_remove;
        for (auto item : sys_modules)
        {
            std::string mod_name = py::str(item.first);

            bool existed_before = false;
            for (auto existing : modules_snapshot)
            {
                if (py::str(existing).cast<std::string>() == mod_name)
                {
                    existed_before = true;
                    break;
                }
            }
            if (existed_before)
                continue;

            try
            {
                py::object mod_obj = py::reinterpret_borrow<py::object>(item.second);
                if (mod_name == module_name)
                {
                    modules_to_remove.push_back(mod_name);
                }
                else if (py::hasattr(mod_obj, "__file__"))
                {
                    std::string mod_file = py::str(mod_obj.attr("__file__"));
                    if (mod_file.find(script_dir) == 0)
                        modules_to_remove.push_back(mod_name);
                }
            }
            catch (...)
            {
            }
        }
        for (const auto &mod_name : modules_to_remove)
            sys_modules.attr("pop")(mod_name, py::none());
    }
    catch (const py::error_already_set &e)
    {
        result.error_message = "Export script initialization failed: " + std::string(e.what());
        BOOST_LOG_TRIVIAL(error) << result.error_message;
    }
    catch (const std::exception &e)
    {
        result.error_message = "Export script error: " + std::string(e.what());
        BOOST_LOG_TRIVIAL(error) << result.error_message;
    }

    return result;
}

} // namespace Luminary
