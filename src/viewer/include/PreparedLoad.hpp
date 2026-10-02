///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include "PathVertex.hpp"
#include "Types.hpp"

#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace libvgcode
{

struct GCodeInputData;
struct PreparedLoadData;

//
// The view types that color by a range, in the order ViewSettings::color_range_palettes holds their palettes
//
static constexpr std::array<EViewType, 10> COLOR_RANGE_VIEW_TYPES = {EViewType::Height,
                                                                     EViewType::Width,
                                                                     EViewType::Speed,
                                                                     EViewType::ActualSpeed,
                                                                     EViewType::FanSpeed,
                                                                     EViewType::Temperature,
                                                                     EViewType::VolumetricFlowRate,
                                                                     EViewType::ActualVolumetricFlowRate,
                                                                     EViewType::LayerTimeLinear,
                                                                     EViewType::LayerTimeLogarithmic};

//
// Every viewer setting the load's enabled lists (with their chunks) and its colors read: the lists read the visibility
// of options and extrusion roles, the top layer only range, the sealed bead and chunk culling switches and whether a
// clipping plane is set; the colors read the view type, the time mode, the visibilities, the role and option colors,
// the palettes of the range view types, the tool palette and the color print palette. Viewer::get_view_settings()
// takes it from a viewer; the load compares it with its own.
//
struct ViewSettings
{
    EViewType view_type{EViewType::FeatureType};
    ETimeMode time_mode{ETimeMode::Normal};
    bool top_layer_only_view_range{false};
    std::array<bool, OPTION_TYPES_COUNT> options_visibility{};
    std::array<bool, GCODE_EXTRUSION_ROLES_COUNT> extrusion_roles_visibility{};
    bool sealed_bead_culling{true};
    bool chunk_culling{true};
    bool clipping_plane{false};
    std::array<Color, GCODE_EXTRUSION_ROLES_COUNT> extrusion_roles_colors{};
    std::array<Color, OPTION_TYPES_COUNT> options_colors{};
    std::array<Palette, COLOR_RANGE_VIEW_TYPES.size()> color_range_palettes;
    Palette tool_colors;
    Palette color_print_colors;
};

//
// The settings that differ between two view snapshots, by name, comma separated (empty when none), and whether the
// enabled lists and the colors read any of them
//
std::string view_settings_difference(const ViewSettings &a, const ViewSettings &b, bool &lists_differ,
                                     bool &colors_differ);

//
// The viewer settings a load's preparation reads, taken from the viewer on the thread that owns it
// (Viewer::get_prepare_settings()). The load corrects the buffers for radii changed since. With a view snapshot the
// preparation also builds the enabled lists, their chunks and the colors, which the load uses when its own view
// settings equal the snapshot.
//
struct PrepareSettings
{
    float travels_radius{DEFAULT_TRAVELS_RADIUS_MM};
    float wipes_radius{DEFAULT_WIPES_RADIUS_MM};
    // Keep the prefilter's per-vertex neighbour data after the buffers are built (a test hook reads it)
    bool keep_prefilter_neighbours{false};
    std::optional<ViewSettings> view;
};

//
// Thrown by a preparation whose cancel test returned true
//
class PrepareCanceled : public std::exception
{
public:
    const char *what() const noexcept override { return "load preparation canceled"; }
};

//
// The CPU part of a load: the vertices with the palettes, the tables of the vertex scan (layers, extents, times,
// options, extrusion roles, used extruders, drawn lines), the load-time passes (the prefilter's neighbour search, the
// sealed bead pass) and the contents of the positions and heights/widths/angles buffers. It owns all of it, holds no
// GL object and no reference to a viewer, and is made by prepare_load() on any thread; Viewer::load() installs it on
// the thread that owns the GL context and takes its data. Move only; one owner at a time.
//
class PreparedLoad
{
public:
    PreparedLoad();
    ~PreparedLoad();
    PreparedLoad(PreparedLoad &&other) noexcept;
    PreparedLoad &operator=(PreparedLoad &&other) noexcept;
    PreparedLoad(const PreparedLoad &other) = delete;
    PreparedLoad &operator=(const PreparedLoad &other) = delete;

    //
    // The vertices to load; empty once moved from or installed.
    //
    const std::vector<PathVertex> &get_vertices() const;
    //
    // Replaces the palettes the load renders with (the tool colors, and the color print colors).
    //
    void set_palettes(Palette tools_colors, Palette color_print_colors);
    //
    // The wall time of the preparation, in milliseconds.
    //
    float get_prepare_ms() const;

    //
    // The prepared tables, null once moved from or installed; read by the viewer's install and by tests.
    //
    PreparedLoadData *data() { return m_data.get(); }
    const PreparedLoadData *data() const { return m_data.get(); }
    //
    // Hands the prepared tables over, leaving this load empty.
    //
    std::unique_ptr<PreparedLoadData> take_data();

private:
    std::unique_ptr<PreparedLoadData> m_data;
};

//
// Prepares a load of the given data: everything load() computes that depends on the vertices and the given settings
// alone. Touches no GL, no viewer and no global state, so it may run on any thread. `progress`, when set, is called on
// the calling thread with the fraction done (never decreasing, 1 at the end). `canceled`, when set, is polled at every
// progress report (the load-time passes report per layer) and every 65536 vertices of the vertex loops; when it
// returns true the preparation stops, joins the passes' threads and throws PrepareCanceled. A failed pass leaves no
// data of its own and says why in its statistics, as load() always did.
//
PreparedLoad prepare_load(GCodeInputData &&gcode_data, const PrepareSettings &settings,
                          const std::function<void(float)> &progress = {}, const std::function<bool()> &canceled = {});

//
// The axis aligned box of the given vertices of the given move types, and of those extruding one of the given roles
// (what Viewer::get_bounding_box() and Viewer::get_extrusion_bounding_box() return for loaded vertices).
//
AABox get_vertices_bounding_box(const std::vector<PathVertex> &vertices,
                                const std::vector<EMoveType> &types = {EMoveType::Retract, EMoveType::Unretract,
                                                                       EMoveType::Seam, EMoveType::ToolChange,
                                                                       EMoveType::ColorChange, EMoveType::PausePrint,
                                                                       EMoveType::CustomGCode, EMoveType::Travel,
                                                                       EMoveType::Wipe, EMoveType::Extrude});
AABox get_vertices_extrusion_bounding_box(const std::vector<PathVertex> &vertices,
                                          const std::vector<EGCodeExtrusionRole> &roles);

} // namespace libvgcode
