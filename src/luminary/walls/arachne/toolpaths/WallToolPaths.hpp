///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) 2020 Ultimaker B.V. - CuraEngine
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/

#pragma once

#include <ankerl/unordered_dense.h>
#include <stddef.h>
#include <memory>
#include <utility>
#include <vector>
#include <cstddef>

#include "luminary/walls/arachne/beading/BeadingStrategyFactory.hpp"
#include "luminary/walls/arachne/paths/ExtrusionLine.hpp"
#include "luminary/geometry/contours/Polygon.hpp"
#include "luminary/config/catalog/PrintConfig.hpp"
#include "luminary/geometry/primitives/Point.hpp"
#include "luminary/core/Prelude.hpp"
#include "luminary/walls/MinWallLength.hpp"

namespace boost
{
template<class T>
struct hash;
} // namespace boost

namespace Luminary::Arachne
{

constexpr bool fill_outline_gaps = true;
constexpr coord_t meshfix_maximum_resolution = scaled<coord_t>(0.5);
constexpr coord_t meshfix_maximum_deviation = scaled<coord_t>(0.025);
constexpr coord_t meshfix_maximum_extrusion_area_deviation = scaled<coord_t>(2.);

class WallToolPaths
{
public:
    /*!
     * A class that creates the toolpaths given an outline, nominal bead width and maximum amount of walls
     * \param outline An outline of the area in which the ToolPaths are to be generated
     * \param bead_width_0 The bead width of the first wall used in the generation of the toolpaths
     * \param bead_width_x The bead width of the inner walls used in the generation of the toolpaths
     * \param inset_count The maximum number of parallel extrusion lines that make up the wall
     * \param wall_0_inset How far to inset the outer wall, to make it adhere better to other walls.
     */
    WallToolPaths(const Polygons &outline, coord_t bead_width_0, coord_t bead_width_x, size_t inset_count,
                  coord_t wall_0_inset, coordf_t layer_height, const PrintObjectConfig &print_object_config,
                  const PrintConfig &print_config);

    /*!
     * Generates the Toolpaths
     * \return A reference to the newly create  ToolPaths
     */
    const std::vector<VariableWidthLines> &generate();

    /*!
     * Gets the toolpaths, if this called before \p generate() it will first generate the Toolpaths
     * \return a reference to the toolpaths
     */
    const std::vector<VariableWidthLines> &getToolPaths();

    /*!
     * Compute the inner contour of the walls. This contour indicates where the walled area ends and its infill begins.
     * The inside can then be filled, e.g. with skin/infill for the walls of a part, or with a pattern in the case of
     * infill with extra infill walls.
     */
    void separateOutInnerContour();

    /*!
     * Gets the inner contour of the area which is inside of the generated tool
     * paths.
     *
     * If the walls haven't been generated yet, this will lazily call the
     * \p generate() function to generate the walls with variable width.
     * The resulting polygon will snugly match the inside of the variable-width
     * walls where the walls get limited by the LimitedBeadingStrategy to a
     * maximum wall count.
     * If there are no walls, the outline will be returned.
     * \return The inner contour of the generated walls.
     */
    const Polygons &getInnerContour();

    /*!
     * Removes empty paths from the toolpaths
     * \param toolpaths the VariableWidthPaths generated with \p generate()
     * \return true if there are still paths left. If all toolpaths were removed it returns false
     */
    static bool removeEmptyToolPaths(std::vector<VariableWidthLines> &toolpaths);

    // The floor under which an odd open wall is dropped. The perimeter generator sets it from the
    // Minimum wall length setting before the toolpaths are generated; left alone, it is the engine
    // floor of half the wall's own width.
    void set_min_wall_length(const MinWallLength &floor) { min_wall_length = floor; }

    // The layer id and print z stamped on this instance's debug lines, so they align with the
    // [PERIM] entries of the same layer; unset for fill-side instances.
    void set_debug_context(int layer_id, double print_z)
    {
        debug_layer_id = layer_id;
        debug_print_z = print_z;
    }

    using ExtrusionLineSet =
        ankerl::unordered_dense::set<std::pair<const ExtrusionLine *, const ExtrusionLine *>,
                                     boost::hash<std::pair<const ExtrusionLine *, const ExtrusionLine *>>>;

protected:
    /*!
     * Stitch the polylines together and form closed polygons.
     *
     * Works on both toolpaths and inner contours simultaneously.
     */
    static void stitchToolPaths(std::vector<VariableWidthLines> &toolpaths, coord_t bead_width_x);

    /*!
     * Remove odd open polylines shorter than the short-wall floor: half the smallest line width
     * along that polyline unless the perimeter generator raised it.
     */
    void removeSmallLines(std::vector<VariableWidthLines> &toolpaths);

    /*!
     * Simplifies the variable-width toolpaths by calling the simplify on every line in the toolpath using the provided
     * settings.
     * \param settings The settings as provided by the user
     * \return
     */
    static void simplifyToolPaths(std::vector<VariableWidthLines> &toolpaths);

private:
    // Owned copy of the designated outline area. The constructor may receive a
    // temporary, so the instance must not keep a reference to caller storage.
    Polygons outline;
    coord_t bead_width_0; //<! The nominal or first extrusion line width with which libArachne generates its walls
    coord_t
        bead_width_x; //<! The subsequently extrusion line width with which libArachne generates its walls if WallToolPaths was called with the nominal_bead_width Constructor this is the same as bead_width_0
    size_t inset_count; //<! The maximum number of walls to generate
    coord_t
        wall_0_inset; //<! How far to inset the outer wall. Should only be applied when printing the actual walls, not extra infill/skin/support walls.
    coordf_t layer_height;
    bool print_thin_walls; //<! Whether to enable the widening beading meta-strategy for thin features
    coord_t
        min_feature_size; //<! The minimum size of the features that can be widened by the widening beading meta-strategy. Features thinner than that will not be printed
    coord_t
        min_bead_width; //<! The minimum bead size to use when widening thin model features with the widening beading meta-strategy
    double
        small_area_length; //<! The length of the small features which are to be filtered out, this is squared into a surface
    coord_t wall_transition_filter_deviation; //!< The allowed line width deviation induced by filtering
    coord_t wall_transition_length;
    float min_nozzle_diameter;
    bool toolpaths_generated;                  //<! Are the toolpaths generated
    std::vector<VariableWidthLines> toolpaths; //<! The generated toolpaths
    Polygons inner_contour;                    //<! The inner contour of the generated toolpaths
    const PrintObjectConfig &print_object_config;
    MinWallLength min_wall_length; //<! The floor under which an odd open wall is dropped
    int debug_layer_id{-1};        //<! Layer id stamped on debug lines (-1 = unknown)
    double debug_print_z{0.0};     //<! Print z stamped on debug lines
};

} // namespace Luminary::Arachne
