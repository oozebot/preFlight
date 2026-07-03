///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "PreProcessor.hpp"
#include "GCodeProcessor.hpp"
#include "GCodeObject.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r_version.h"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/embed.h>
#include <pybind11/functional.h>

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_set>

namespace py = pybind11;

namespace Slic3r
{

// -------------------------------------------------------------------------
// Layer wrapper: groups moves by layer_id and provides prepend/append
// -------------------------------------------------------------------------
struct PyLayer
{
    unsigned int id;
    float z;
    float height;
    std::vector<GCodeProcessorResult::MoveVertex *> moves;
    float time;

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

    // Build layer structure from flat moves vector
    void build_layers()
    {
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

        for (auto &mv : result->moves)
        {
            if (mv.gcode_id == 0 || mv.layer_id > max_layer)
                continue;
            auto &layer = layers[mv.layer_id];
            layer.moves.push_back(&mv);
            layer.time += mv.time[0];
            if (mv.position.z() > layer.z)
                layer.z = mv.position.z();
        }

        // Compute layer heights as deltas
        for (size_t i = 1; i < layers.size(); ++i)
            layers[i].height = layers[i].z - layers[i - 1].z;
        if (!layers.empty())
            layers[0].height = layers[0].z;

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
        size_t total_lines = gcode_object ? gcode_object->line_count() : 0;
        if (line == 0 || line > total_lines)
        {
            BOOST_LOG_TRIVIAL(warning) << "Pre-processor: insert() line " << line << " is out of range (file has "
                                       << total_lines << " lines)";
            return;
        }
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
        if (comment.empty())
            line_replacements[line_id] = gcode;
        else
            line_replacements[line_id] = gcode + " ; " + comment;
    }

    unsigned int line_count() const { return gcode_object ? static_cast<unsigned int>(gcode_object->line_count()) : 0; }

    // Return the 1-based line_id of the first line containing text, or 0 if not found
    unsigned int find_line(const std::string &text) const
    {
        size_t count = gcode_object ? gcode_object->line_count() : 0;
        for (size_t i = 0; i < count; ++i)
        {
            std::string line = std::string(gcode_object->get_line_text(i));
            if (line.find(text) != std::string::npos)
                return static_cast<unsigned int>(i + 1);
        }
        return 0;
    }

    // Return all 1-based line_ids containing text
    py::list find_lines(const std::string &text) const
    {
        py::list found;
        size_t count = gcode_object ? gcode_object->line_count() : 0;
        for (size_t i = 0; i < count; ++i)
        {
            std::string line = std::string(gcode_object->get_line_text(i));
            if (line.find(text) != std::string::npos)
                found.append(static_cast<unsigned int>(i + 1));
        }
        return found;
    }

    int remove_moves(py::function predicate)
    {
        int count = 0;
        for (auto &mv : result->moves)
        {
            if (mv.gcode_id == 0)
                continue;
            if (predicate(py::cast(&mv, py::return_value_policy::reference)).cast<bool>())
            {
                removed_move_ids.insert(mv.gcode_id);
                ++count;
            }
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
            if (mv.gcode_id == 0)
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

    m.attr("version") = SLIC3R_VERSION;
    m.attr("exe_dir") = ""; // populated after interpreter init

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
    py::enum_<EMoveType>(m, "MoveType")
        .value("Noop", EMoveType::Noop)
        .value("Retract", EMoveType::Retract)
        .value("Unretract", EMoveType::Unretract)
        .value("Seam", EMoveType::Seam)
        .value("ToolChange", EMoveType::Tool_change)
        .value("ColorChange", EMoveType::Color_change)
        .value("PausePrint", EMoveType::Pause_Print)
        .value("CustomGCode", EMoveType::Custom_GCode)
        .value("Travel", EMoveType::Travel)
        .value("Wipe", EMoveType::Wipe)
        .value("Extrude", EMoveType::Extrude);

    // GCodeExtrusionRole enum
    py::enum_<GCodeExtrusionRole>(m, "ExtrusionRole")
        .value("NoRole", GCodeExtrusionRole::None)
        .value("Perimeter", GCodeExtrusionRole::Perimeter)
        .value("ExternalPerimeter", GCodeExtrusionRole::ExternalPerimeter)
        .value("OverhangPerimeter", GCodeExtrusionRole::OverhangPerimeter)
        .value("InterlockingPerimeter", GCodeExtrusionRole::InterlockingPerimeter)
        .value("InternalInfill", GCodeExtrusionRole::InternalInfill)
        .value("SolidInfill", GCodeExtrusionRole::SolidInfill)
        .value("TopSolidInfill", GCodeExtrusionRole::TopSolidInfill)
        .value("Ironing", GCodeExtrusionRole::Ironing)
        .value("BridgeInfill", GCodeExtrusionRole::BridgeInfill)
        .value("GapFill", GCodeExtrusionRole::GapFill)
        .value("Skirt", GCodeExtrusionRole::Skirt)
        .value("SupportMaterial", GCodeExtrusionRole::SupportMaterial)
        .value("SupportMaterialInterface", GCodeExtrusionRole::SupportMaterialInterface)
        .value("WipeTower", GCodeExtrusionRole::WipeTower)
        .value("Custom", GCodeExtrusionRole::Custom)
        .value("Serpentine", GCodeExtrusionRole::Serpentine)
        .value("SerpentineOverhang", GCodeExtrusionRole::SerpentineOverhang);

    // CustomGCode::Type enum
    py::enum_<CustomGCode::Type>(m, "CustomEventType")
        .value("ColorChange", CustomGCode::ColorChange)
        .value("PausePrint", CustomGCode::PausePrint)
        .value("ToolChange", CustomGCode::ToolChange)
        .value("Template", CustomGCode::Template)
        .value("Custom", CustomGCode::Custom);

    // FilamentUsage data class
    py::class_<PyFilamentUsage>(m, "FilamentUsage")
        .def_readonly("meters", &PyFilamentUsage::meters)
        .def_readonly("grams", &PyFilamentUsage::grams)
        .def_readonly("volume_mm3", &PyFilamentUsage::volume_mm3)
        .def_readonly("cost", &PyFilamentUsage::cost);

    // CustomEvent data class
    py::class_<PyCustomEvent>(m, "CustomEvent")
        .def_readonly("z", &PyCustomEvent::z)
        .def_readonly("type", &PyCustomEvent::type)
        .def_readonly("extruder", &PyCustomEvent::extruder)
        .def_readonly("color", &PyCustomEvent::color)
        .def_readonly("extra", &PyCustomEvent::extra);

    // MoveVertex bindings
    using MV = GCodeProcessorResult::MoveVertex;
    py::class_<MV>(m, "Move")
        // Read/write properties
        .def_readwrite("feedrate", &MV::feedrate)
        .def_readwrite("fan_speed", &MV::fan_speed)
        .def_readwrite("temperature", &MV::temperature)
        .def_readwrite("delta_e", &MV::delta_extruder)
        .def_readwrite("width", &MV::width)
        .def_readwrite("height", &MV::height)
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
            })
        // Read-only properties
        .def_readonly("type", &MV::type)
        .def_readonly("role", &MV::extrusion_role)
        .def_readonly("extruder_id", &MV::extruder_id)
        .def_readonly("color_id", &MV::cp_color_id)
        .def_readonly("mm3_per_mm", &MV::mm3_per_mm)
        .def_readonly("actual_feedrate", &MV::actual_feedrate)
        .def_readonly("gcode_line_id", &MV::gcode_id)
        .def_readonly("layer_id", &MV::layer_id)
        .def_readonly("internal_only", &MV::internal_only)
        // Position as individual floats
        .def_property_readonly("x", [](const MV &mv) { return mv.position.x(); })
        .def_property_readonly("y", [](const MV &mv) { return mv.position.y(); })
        .def_property_readonly("z", [](const MV &mv) { return mv.position.z(); })
        // Computed properties
        .def_property_readonly("volumetric_rate", &MV::volumetric_rate)
        .def_property_readonly("actual_volumetric_rate", &MV::actual_volumetric_rate)
        // Time (normal mode)
        .def_property_readonly("time", [](const MV &mv) { return mv.time[0]; })
        .def_property_readonly("time_stealth", [](const MV &mv) { return mv.time[1]; })
        // Motion analysis
        .def_readonly("distance", &MV::distance)
        .def_readonly("junction_angle", &MV::junction_angle)
        .def_property_readonly("acceleration", [](const MV &mv) { return mv.acceleration[0]; })
        .def_property_readonly("acceleration_stealth", [](const MV &mv) { return mv.acceleration[1]; })
        .def_property_readonly("max_entry_speed", [](const MV &mv) { return mv.max_entry_speed[0]; })
        .def_property_readonly("max_entry_speed_stealth", [](const MV &mv) { return mv.max_entry_speed[1]; })
        // Fill region properties
        .def_readonly("region_area", &MV::region_area)
        .def_property_readonly("fill_pattern",
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
                               });

    // Layer bindings
    py::class_<PyLayer>(m, "Layer")
        .def_readonly("id", &PyLayer::id)
        .def_readonly("z", &PyLayer::z)
        .def_readonly("height", &PyLayer::height)
        .def_readonly("time", &PyLayer::time)
        .def_readwrite("moves", &PyLayer::moves)
        .def("prepend", &PyLayer::prepend, py::arg("gcode"), py::arg("comment") = "")
        .def("append", &PyLayer::append, py::arg("gcode"), py::arg("comment") = "")
        .def("moves_by_type", &PyLayer::moves_by_type, py::arg("move_type"),
             py::return_value_policy::reference_internal)
        .def("moves_by_role", &PyLayer::moves_by_role, py::arg("role"), py::return_value_policy::reference_internal)
        .def("extrusion_length", &PyLayer::extrusion_length)
        .def("travel_distance", &PyLayer::travel_distance);

    // Top-level GCode bindings
    py::class_<PyGCode>(m, "GCode")
        .def_readonly("layers", &PyGCode::layers)
        .def_readonly("max_print_height", &PyGCode::max_print_height)
        .def_readonly("extruder_count", &PyGCode::extruder_count)
        .def_readonly("extruder_colors", &PyGCode::extruder_colors)
        .def_readonly("spiral_vase_mode", &PyGCode::spiral_vase_mode)
        .def_readwrite("annotation", &PyGCode::annotation)
        .def_property_readonly("time_estimate_normal", &PyGCode::time_estimate_normal)
        .def_property_readonly("time_estimate_stealth", &PyGCode::time_estimate_stealth)
        .def_property_readonly("first_layer_time", &PyGCode::first_layer_time)
        // Cost data
        .def_property_readonly(
            "filament_cost", [](PyGCode &g) -> const std::vector<float> & { return g.result->filament_cost; },
            py::return_value_policy::reference_internal)
        .def_property_readonly("time_cost", [](PyGCode &g) { return g.result->time_cost; })
        .def_property_readonly("currency_symbol", [](PyGCode &g) { return g.result->currency_symbol; })
        // Active preset names
        .def_property_readonly("preset_print", [](PyGCode &g) { return g.result->settings_ids.print; })
        .def_property_readonly("preset_filament", [](PyGCode &g) { return g.result->settings_ids.filament; })
        .def_property_readonly("preset_printer", [](PyGCode &g) { return g.result->settings_ids.printer; })
        // Flat access to all moves (filtered only if removals have occurred)
        .def_property_readonly("moves",
                               [](PyGCode &g) -> py::object
                               {
                                   // Fast path: no removals, return direct reference to the C++ vector
                                   if (g.removed_move_ids.empty())
                                       return py::cast(g.result->moves, py::return_value_policy::reference_internal,
                                                       py::cast(g));
                                   // Slow path: filter out removed moves
                                   py::list live;
                                   for (auto &mv : g.result->moves)
                                       if (mv.gcode_id != 0)
                                           live.append(py::cast(&mv, py::return_value_policy::reference));
                                   return live;
                               })
        // G-code insertion/replacement
        .def("insert", &PyGCode::insert, py::arg("line"), py::arg("gcode"), py::arg("position") = "after",
             py::arg("comment") = "")
        .def("get_line", &PyGCode::get_line, py::arg("line_id"))
        .def("rewrite", &PyGCode::rewrite, py::arg("line_id"), py::arg("gcode"), py::arg("comment") = "")
        .def_property_readonly("line_count", &PyGCode::line_count)
        .def("find_line", &PyGCode::find_line, py::arg("text"))
        .def("find_lines", &PyGCode::find_lines, py::arg("text"))
        // Move query and removal
        .def("find_moves", &PyGCode::find_moves)
        .def("remove_moves", &PyGCode::remove_moves, py::arg("predicate"))
        // Geometry
        .def_property_readonly("z_offset", [](PyGCode &g) { return g.result->z_offset; })
        .def_property_readonly("bed_shape",
                               [](PyGCode &g)
                               {
                                   py::list shape;
                                   for (const auto &pt : g.result->bed_shape)
                                       shape.append(py::make_tuple(pt.x(), pt.y()));
                                   return shape;
                               })
        // All slicer settings - supports both dot and bracket access
        .def_property_readonly("settings",
                               [](PyGCode &g)
                               {
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
                                   return wrapper(data);
                               })
        // Filament configuration
        .def_property_readonly(
            "filament_diameters", [](PyGCode &g) -> const std::vector<float> & { return g.result->filament_diameters; },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "filament_densities", [](PyGCode &g) -> const std::vector<float> & { return g.result->filament_densities; },
            py::return_value_policy::reference_internal)
        // Filament usage per role: {ExtrusionRole -> FilamentUsage}
        .def_property_readonly("filament_by_role",
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
                               })
        // Filament usage per extruder: {extruder_id -> FilamentUsage}
        .def_property_readonly("filament_by_extruder",
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
                                       if (ext_id < g.result->filament_densities.size() &&
                                           g.result->filament_densities[ext_id] > 0.0f)
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
                               })
        // Filament volumes per color change segment
        .def_property_readonly(
            "filament_by_color_change", [](PyGCode &g) -> const std::vector<double> &
            { return g.result->print_statistics.volumes_per_color_change; },
            py::return_value_policy::reference_internal)
        // Custom G-code events (color changes, pauses, etc.)
        .def_property_readonly("custom_events",
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
                               })
        // Performance metrics per extrusion role
        .def_property_readonly("role_metrics",
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
                               })
        // Overall performance metrics
        .def_property_readonly("overall_metrics",
                               [](PyGCode &g)
                               {
                                   py::dict result;
                                   result["max_commands_per_sec"] = g.result->overall_metrics.max_commands_per_sec;
                                   result["max_layer"] = g.result->overall_metrics.max_layer;
                                   return result;
                               })
        // Object collision detection result
        .def_property_readonly("conflict",
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
                               });

    // Export script API (lightweight - no moves/layers/settings)
    py::class_<PyExportGCode>(m, "ExportGCode")
        .def_readwrite("data", &PyExportGCode::data)
        .def_readonly("filename", &PyExportGCode::filename);
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

static GCodeObject *materialize_modifications(GCodeObject *input, const PreProcessorResult &pp_result,
                                              std::unordered_map<unsigned int, unsigned int> &id_remap)
{
    auto output = std::make_unique<GCodeObject>();

    float current_fan = 0.0f;
    float current_temp = 0.0f;
    double e_offset = 0.0;
    bool relative_e = false;

    for (size_t line_idx = 0; line_idx < input->line_count(); ++line_idx)
    {
        unsigned int old_line_id = static_cast<unsigned int>(line_idx + 1);
        std::string gcode_line(input->get_line_text(line_idx));

        // Skip removed lines
        if (pp_result.has_removals() && pp_result.removed_lines.count(old_line_id))
            continue;

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
            if (pp_it != pp_result.modifications.end())
            {
                const auto &mod = pp_it->second;

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

                // Insert M106 only if the script changed fan speed
                if (mod.fan_speed != mod.original_fan_speed)
                {
                    char fan_buf[64];
                    snprintf(fan_buf, sizeof(fan_buf), "M106 S%.0f", mod.fan_speed * 255.0f / 100.0f);
                    write_text(output.get(), fan_buf);
                }

                // Insert M104 only if the script changed temperature
                if (mod.temperature != mod.original_temperature)
                {
                    if (mod.temperature > 500.0f)
                        BOOST_LOG_TRIVIAL(warning) << "Pre-processor: temperature " << mod.temperature << "C on line "
                                                   << old_line_id << " exceeds 500C - verify this is intentional";
                    char temp_buf[64];
                    snprintf(temp_buf, sizeof(temp_buf), "M104 S%.0f", mod.temperature);
                    write_text(output.get(), temp_buf);
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
            if (GCodeReader::GCodeLine::cmd_is(gcode_line, "M106"))
            {
                size_t s_pos = gcode_line.find('S');
                if (s_pos != std::string::npos)
                    current_fan = safe_stof(gcode_line.substr(s_pos + 1)) * 100.0f / 255.0f;
            }
            else if (GCodeReader::GCodeLine::cmd_is(gcode_line, "M107"))
            {
                current_fan = 0.0f;
            }
            else if (GCodeReader::GCodeLine::cmd_is(gcode_line, "M104"))
            {
                size_t s_pos = gcode_line.find('S');
                if (s_pos != std::string::npos)
                    current_temp = safe_stof(gcode_line.substr(s_pos + 1));
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
                size_t e_pos = gcode_line.find('E');
                if (e_pos != std::string::npos)
                    e_offset = 0.0;
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

            pp_result.modifications[mv.gcode_id] = {mv.feedrate,       mv.fan_speed,  mv.temperature,
                                                    mv.delta_extruder, snap.feedrate, snap.fan_speed,
                                                    snap.temperature,  snap.delta_e,  move_annotation};
        }
    }

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
        throw Slic3r::RuntimeError("Python interpreter initialization failed");

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

os.chdir(preFlight.exe_dir)
)");
                       s_python_setup_ok = true;
                   });

    if (!s_python_setup_ok)
        throw Slic3r::RuntimeError("Python post-init setup failed");

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
                                             const std::string &resources_dir, const Print *print)
{
    PreProcessorResult pp_result;
    if (script_paths.empty())
        return pp_result;

    PyGCode gcode;
    gcode.result = &result;
    gcode.gcode_object = gcode_object;
    gcode.print = print;
    gcode.build_layers();

    std::atomic<bool> scripts_running{true};
    std::thread cancel_watchdog;

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

        // signal.signal() and PyErr_SetInterrupt() only work on the interpreter's
        // main thread. Slicing runs on a background worker, so detect that and skip
        // signal-based cancellation there - the per-script canceled() check in the
        // loop below still aborts between scripts.
        bool is_main_thread = false;
        try
        {
            py::module_ threading_mod = py::module_::import("threading");
            is_main_thread = threading_mod.attr("current_thread")().is(threading_mod.attr("main_thread")());
        }
        catch (...)
        {
        }

        // Watchdog thread: polls the cancel flag and interrupts Python if canceled
        if (print && is_main_thread)
        {
            cancel_watchdog = std::thread(
                [print, &scripts_running]()
                {
                    while (scripts_running.load(std::memory_order_relaxed))
                    {
                        if (print->canceled())
                        {
                            PyErr_SetInterrupt();
                            return;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                });
        }

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

        // Re-set CWD to exe directory before each slice in case a previous
        // script changed it (os.chdir is process-global).
        auto exe_dir = boost::dll::program_location().parent_path();
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

                // Add script dir to sys.path for import resolution
                py::list path = sys_mod.attr("path");
                std::string script_dir = fs::path(script_path).parent_path().string();
                path.attr("insert")(0, script_dir);

                py::module_ script_mod = py::module_::import(fs::path(script_path).stem().string().c_str());

                if (py::hasattr(script_mod, "process"))
                {
                    gcode.annotation.clear();
                    gcode.move_annotations.clear();
                    s_active_gcode = &gcode;
                    script_mod.attr("process")(py::cast(&gcode, py::return_value_policy::reference));
                    s_active_gcode = nullptr;
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
                if (e.matches(PyExc_KeyboardInterrupt))
                {
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

        // Restore CWD in case a script changed it
        os_mod.attr("chdir")(exe_dir.string());

        // Warn about lingering threads (scripts may have spawned daemon threads)
        try
        {
            py::module_ threading = py::module_::import("threading");
            py::list threads = threading.attr("enumerate")();
            int non_main_count = 0;
            for (auto t : threads)
            {
                if (!t.attr("daemon").cast<bool>() && t.attr("name").cast<std::string>() != "MainThread")
                    continue; // non-daemon, non-main threads are fine (they're our own)
                if (t.attr("daemon").cast<bool>())
                    non_main_count++;
            }
            if (non_main_count > 0)
                BOOST_LOG_TRIVIAL(warning) << "Pre-processor: " << non_main_count
                                           << " daemon thread(s) still running after scripts finished. "
                                              "Scripts should not spawn persistent threads.";
        }
        catch (...)
        {
            // threading module unavailable or enumerate failed - not critical
        }

        // Stop the watchdog thread
        scripts_running.store(false, std::memory_order_relaxed);
        if (cancel_watchdog.joinable())
            cancel_watchdog.join();

        // Update the caller's GCodeObject pointer if we materialized
        if (gcode.gcode_object != gcode_object)
        {
            gcode_object = gcode.gcode_object;
        }
    }
    catch (const std::exception &e)
    {
        scripts_running.store(false, std::memory_order_relaxed);
        if (cancel_watchdog.joinable())
            cancel_watchdog.join();
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
// -------------------------------------------------------------------------
// Module names that must never be used as export script filenames
static const std::unordered_set<std::string> s_reserved_module_names = {
    "os",          "sys",        "io",       "re",       "ssl",      "json",   "csv",       "ftplib",
    "socket",      "http",       "urllib",   "email",    "logging",  "math",   "time",      "datetime",
    "threading",   "subprocess", "ctypes",   "struct",   "hashlib",  "base64", "shutil",    "pathlib",
    "tempfile",    "signal",     "queue",    "copy",     "types",    "abc",    "functools", "itertools",
    "collections", "preFlight",  "pybind11", "builtins", "importlib"};

ExportScriptResult run_export_script(const std::string &script_path, const std::string &gcode_buffer,
                                     const std::string &filename)
{
    ExportScriptResult result;
    namespace fs = boost::filesystem;

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
                script_mod.attr("export")(py::cast(&gcode, py::return_value_policy::reference));
                BOOST_LOG_TRIVIAL(info) << "Export script: " << script_name << " completed successfully";
                result.success = true;
            }
            else
            {
                result.error_message = "Export script '" + script_name + "' has no export() function";
                BOOST_LOG_TRIVIAL(error) << result.error_message;
            }
        }
        catch (const py::error_already_set &e)
        {
            if (e.matches(PyExc_SystemExit))
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

} // namespace Slic3r
