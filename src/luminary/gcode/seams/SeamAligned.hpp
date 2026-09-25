///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

#include "SeamPerimeters.hpp"
#include "SeamChoice.hpp"
#include "luminary/geometry/primitives/Point.hpp"
#include "SeamShells.hpp"

namespace Luminary::ModelInfo
{
struct Visibility;
}

namespace Luminary::Seams::Aligned
{

using SeamChoiceVisibility = std::function<double(const SeamChoice &, const Perimeters::Perimeter &)>;

namespace Impl
{
struct SeamOptions
{
    std::size_t closest;
    std::size_t adjacent;
    bool adjacent_forward;
    std::optional<std::size_t> snapped;
    Vec2d on_edge;
};

SeamChoice pick_seam_option(const Perimeters::Perimeter &perimeter, const SeamOptions &options);

std::optional<std::size_t> snap_to_angle(const Vec2d &point, const std::size_t search_start,
                                         const Perimeters::Perimeter &perimeter, const double max_detour);

SeamOptions get_seam_options(const Perimeters::Perimeter &perimeter, const Vec2d &prefered_position,
                             const Perimeters::Perimeter::PointTree &points_tree, const double max_detour);

struct Nearest
{
    Vec2d prefered_position;
    double max_detour;

    std::optional<SeamChoice> operator()(const Perimeters::Perimeter &perimeter, const Perimeters::PointType point_type,
                                         const Perimeters::PointClassification point_classification) const;
};

struct LeastVisible
{
    std::optional<SeamChoice> operator()(const Perimeters::Perimeter &perimeter, const Perimeters::PointType point_type,
                                         const Perimeters::PointClassification point_classification) const;

    const std::vector<double> &precalculated_visibility;
};

// Paint is a target, not a fence: a perimeter that carries paint seams at the painted run's
// arc-length midpoint, the run nearest the reference when there are several. Empty when the
// perimeter carries no paint.
std::optional<SeamChoice> choose_painted_seam(const Perimeters::Perimeter &perimeter, const Vec2d &reference_position,
                                              const double max_detour);
} // namespace Impl

struct VisibilityCalculator
{
    const Luminary::ModelInfo::Visibility &points_visibility;
    double convex_visibility_modifier;
    double concave_visibility_modifier;

    double operator()(const SeamChoice &choice, const Perimeters::Perimeter &perimeter) const;

private:
    static double get_angle_visibility_modifier(const double angle, const double convex_visibility_modifier,
                                                const double concave_visibility_modifier);
};

struct Params
{
    double max_detour{};
    double jump_visibility_threshold{};
    double continuity_modifier{};
};

std::vector<std::vector<SeamPerimeterChoice>> get_object_seams(Shells::Shells<> &&shells,
                                                               const SeamChoiceVisibility &visibility_calculator,
                                                               const Params &params);

// The painted column of a shell, one entry per slice: the trend-filtered paint centerline of a
// painted slice, a paint-free slice between two painted ones interpolated between them, nothing
// below the first or above the last painted slice. Every seam mode places its painted
// perimeters on it, so a painted line reads the same whichever mode the rest of the object uses.
using PaintedColumn = std::vector<std::optional<Vec2d>>;
std::vector<PaintedColumn> get_painted_columns(const Shells::Shells<> &shells);

// The seam of a perimeter on its painted column: the column position itself, on the perimeter.
SeamChoice choose_column_seam(const Perimeters::Perimeter &perimeter, const Vec2d &column, const double max_detour);

} // namespace Luminary::Seams::Aligned
