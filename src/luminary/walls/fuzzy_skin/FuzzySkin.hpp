///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

// Polygon at fuzzy_polygon and Polygons at the apply_fuzzy_skin declarations; the precompiled
// header had been supplying this header to every translation unit and luminary uses none.
#include "luminary/geometry/contours/Polygon.hpp"

#include "NoiseGenerator.hpp"

namespace Luminary::Arachne
{
struct ExtrusionLine;
} // namespace Luminary::Arachne

namespace Luminary::Athena
{
struct ExtrusionLine;
} // namespace Luminary::Athena

namespace Luminary::PerimeterGenerator
{
struct Parameters;
} // namespace Luminary::PerimeterGenerator

namespace Luminary
{
class Layer;
} // namespace Luminary

namespace Luminary::Feature::FuzzySkin
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

} // namespace Luminary::Feature::FuzzySkin
