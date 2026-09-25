///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2020 - 2022 Pavel Mikuš @Godrak, Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <optional>
#include <vector>
#include <memory>
#include <atomic>

#include "SeamAligned.hpp"
#include "SeamScarf.hpp"
#include "luminary/geometry/contours/Polygon.hpp"
#include "luminary/layer/print/Print.hpp"
#include "luminary/geometry/primitives/Point.hpp"
#include "SeamPerimeters.hpp"
#include "SeamChoice.hpp"

namespace SeamGlobalParams
{
// Global storage for seam parameter set by UI
inline float s_painting_radius = 0.05f;

inline void setSeamDetectionRadius(float radius)
{
    s_painting_radius = radius;
}
inline float getSeamDetectionRadius()
{
    return s_painting_radius;
}
} // namespace SeamGlobalParams
#include "luminary/gcode/seams/model_queries/ModelVisibility.hpp"

namespace Luminary::Seams
{

using ObjectSeams = std::unordered_map<const PrintObject *, std::vector<std::vector<SeamPerimeterChoice>>>;
using ObjectLayerPerimeters = std::unordered_map<const PrintObject *, Perimeters::LayerPerimeters>;

struct Params
{
    double max_nearest_detour;
    double rear_tolerance;
    double rear_y_offset;
    Aligned::Params aligned;
    double max_distance{};
    unsigned random_seed{};
    double convex_visibility_modifier{};
    double concave_visibility_modifier{};
    Perimeters::PerimeterParams perimeter;
    Luminary::ModelInfo::Visibility::Params visibility;
    bool staggered_inner_seams{};
};

std::ostream &operator<<(std::ostream &os, const Params &params);

class Placer
{
public:
    static Params get_params(const DynamicPrintConfig &config);

    void init(SpanOfConstPtrs<PrintObject> objects, const Params &params,
              const std::function<void(void)> &throw_if_canceled);

    boost::variant<Point, Scarf::Scarf> place_seam(const Layer *layer, const PrintRegion *region,
                                                   const ExtrusionLoop &loop, const bool flipped,
                                                   const Point &last_pos) const;

private:
    Params params;
    ObjectSeams seams_per_object;
    ObjectLayerPerimeters perimeters_per_layer;
    // Nearest mode, painted objects only: per layer, the painted column position of each perimeter
    // in perimeters_per_layer, empty for a perimeter off the paint.
    std::map<const PrintObject *, std::vector<std::vector<std::optional<Vec2d>>>> painted_columns_per_layer;
};

} // namespace Luminary::Seams
