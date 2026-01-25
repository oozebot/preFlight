///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef libslic3r_FuzzySkin_hpp_
#define libslic3r_FuzzySkin_hpp_

#include "NoiseGenerator.hpp"

namespace Slic3r::Arachne
{
struct ExtrusionLine;
} // namespace Slic3r::Arachne

namespace Slic3r::Athena
{
struct ExtrusionLine;
} // namespace Slic3r::Athena

namespace Slic3r::PerimeterGenerator
{
struct Parameters;
} // namespace Slic3r::PerimeterGenerator

namespace Slic3r
{
class Layer;
} // namespace Slic3r

namespace Slic3r::Feature::FuzzySkin
{

// Legacy API (backward compatible - uses random noise)
void fuzzy_polygon(Polygon &polygon, double fuzzy_skin_thickness, double fuzzy_skin_point_distance);

// New API with structured noise support
void fuzzy_polyline(Points &poly, bool closed, double slice_z, const FuzzySkinConfig &cfg);
void fuzzy_polygon(Polygon &polygon, double slice_z, const FuzzySkinConfig &cfg);

// Arachne version of fuzzy_extrusion_line
void fuzzy_extrusion_line(Arachne::ExtrusionLine &ext_lines, double fuzzy_skin_thickness, double fuzzy_skin_point_dist);
// New API with structured noise support
void fuzzy_extrusion_line(Arachne::ExtrusionLine &ext_lines, double slice_z, const FuzzySkinConfig &cfg);

// Athena overload for fuzzy_extrusion_line
void fuzzy_extrusion_line(Athena::ExtrusionLine &ext_lines, double fuzzy_skin_thickness, double fuzzy_skin_point_dist);
// New API with structured noise support
void fuzzy_extrusion_line(Athena::ExtrusionLine &ext_lines, double slice_z, const FuzzySkinConfig &cfg);

// Updated should_fuzzify to use FuzzySkinConfig
bool should_fuzzify(const FuzzySkinConfig &config, int layer_id, size_t loop_idx, bool is_contour);
// Legacy API
bool should_fuzzify(const PrintRegionConfig &config, size_t layer_idx, size_t perimeter_idx, bool is_contour);

// Helper to create FuzzySkinConfig from PrintRegionConfig
FuzzySkinConfig make_fuzzy_config(const PrintRegionConfig &config);

// Added Layer* parameter for per-segment visibility checks
// The Layer* parameter allows checking if each segment is on a top/bottom surface before fuzzifying.
// Pass nullptr to skip visibility checking (original behavior).
// Added lower_slices parameter to exclude overhangs
// The lower_slices parameter (offset by half nozzle diameter) is used to detect overhangs.
// Points NOT inside lower_slices are overhangs and will NOT be fuzzified.
// Added ext_perimeter_width for visibility check diameter scaling
Polygon apply_fuzzy_skin(const Polygon &polygon, const PrintRegionConfig &base_config,
                         const PerimeterRegions &perimeter_regions, size_t layer_idx, size_t perimeter_idx,
                         bool is_contour, const Layer *layer = nullptr, const Polygons *lower_slices = nullptr,
                         coord_t ext_perimeter_width = 0);

// New apply_fuzzy_skin with slice_z for structured noise
Polygon apply_fuzzy_skin(const Polygon &polygon, const PrintRegionConfig &base_config,
                         const PerimeterRegions &perimeter_regions, size_t layer_idx, size_t perimeter_idx,
                         bool is_contour, double slice_z, const Layer *layer = nullptr,
                         const Polygons *lower_slices = nullptr, coord_t ext_perimeter_width = 0);

// Arachne version of apply_fuzzy_skin
Arachne::ExtrusionLine apply_fuzzy_skin(const Arachne::ExtrusionLine &extrusion, const PrintRegionConfig &base_config,
                                        const PerimeterRegions &perimeter_regions, size_t layer_idx,
                                        size_t perimeter_idx, bool is_contour, const Layer *layer = nullptr,
                                        const Polygons *lower_slices = nullptr, coord_t ext_perimeter_width = 0);
// New apply_fuzzy_skin with slice_z for structured noise
Arachne::ExtrusionLine apply_fuzzy_skin(const Arachne::ExtrusionLine &extrusion, const PrintRegionConfig &base_config,
                                        const PerimeterRegions &perimeter_regions, size_t layer_idx,
                                        size_t perimeter_idx, bool is_contour, double slice_z,
                                        const Layer *layer = nullptr, const Polygons *lower_slices = nullptr,
                                        coord_t ext_perimeter_width = 0);

// Athena overload for apply_fuzzy_skin
Athena::ExtrusionLine apply_fuzzy_skin(const Athena::ExtrusionLine &extrusion, const PrintRegionConfig &base_config,
                                       const PerimeterRegions &perimeter_regions, size_t layer_idx,
                                       size_t perimeter_idx, bool is_contour, const Layer *layer = nullptr,
                                       const Polygons *lower_slices = nullptr, coord_t ext_perimeter_width = 0);
// New apply_fuzzy_skin with slice_z for structured noise
Athena::ExtrusionLine apply_fuzzy_skin(const Athena::ExtrusionLine &extrusion, const PrintRegionConfig &base_config,
                                       const PerimeterRegions &perimeter_regions, size_t layer_idx,
                                       size_t perimeter_idx, bool is_contour, double slice_z,
                                       const Layer *layer = nullptr, const Polygons *lower_slices = nullptr,
                                       coord_t ext_perimeter_width = 0);

// Clear visibility cache after slicing completes
// Call this after perimeter generation to prevent stale cache data affecting subsequent slices
void clear_visibility_cache();

// Visibility splitting for flow reduction
// Segment of a polygon split at visibility boundaries
struct VisibilitySegment
{
    Points points;
    bool is_visible; // true = visible from top, should have reduced flow
};

// Split a polygon at visibility boundaries, returning segments with visibility state.
// Uses the same algorithm as fuzzy skin for detecting top surface visibility.
// Parameters:
//   polygon: The polygon to split
//   layer: The layer for visibility checking (required)
//   config: PrintRegionConfig for visibility detection settings
//   ext_perimeter_width: External perimeter width for check diameter scaling
// Returns: Vector of segments, each with points and visibility flag
std::vector<VisibilitySegment> split_polygon_by_visibility(const Polygon &polygon, const Layer *layer,
                                                           const PrintRegionConfig &config,
                                                           coord_t ext_perimeter_width);

} // namespace Slic3r::Feature::FuzzySkin

#endif // libslic3r_FuzzySkin_hpp_
