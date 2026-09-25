///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "SeamAligned.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

#include "luminary/core/diagnostics/DebugCounters.hpp"
#include "SeamGeometry.hpp"
#include "luminary/gcode/seams/model_queries/ModelVisibility.hpp"
#include "luminary/geometry/index/KDTreeIndirect.hpp"
#include "luminary/geometry/primitives/Line.hpp"
#include "tcbspan/span.hpp"

namespace Luminary::Seams::Aligned
{
using Perimeters::PointType;
using Perimeters::PointClassification;

namespace Impl
{
const Perimeters::Perimeter::PointTrees &pick_trees(const Perimeters::Perimeter &perimeter, const PointType point_type)
{
    switch (point_type)
    {
    case PointType::enforcer:
        return perimeter.enforced_points;
    case PointType::blocker:
        return perimeter.blocked_points;
    case PointType::common:
        return perimeter.common_points;
    }
    throw std::runtime_error("Point trees for point type do not exist.");
}

const Perimeters::Perimeter::OptionalPointTree &pick_tree(const Perimeters::Perimeter::PointTrees &point_trees,
                                                          const PointClassification &point_classification)
{
    switch (point_classification)
    {
    case PointClassification::overhang:
        return point_trees.overhanging_points;
    case PointClassification::embedded:
        return point_trees.embedded_points;
    case PointClassification::common:
        return point_trees.common_points;
    }
    throw std::runtime_error("Point tree for classification does not exist.");
}

SeamChoice pick_seam_option(const Perimeters::Perimeter &perimeter, const SeamOptions &options)
{
    const std::vector<PointType> &types{perimeter.point_types};
    const std::vector<PointClassification> &classifications{perimeter.point_classifications};
    const std::vector<Vec2d> &positions{perimeter.positions};

    unsigned closeset_point_value = get_point_value(types.at(options.closest), classifications[options.closest]);

    if (options.snapped)
    {
        unsigned snapped_point_value = get_point_value(types.at(*options.snapped), classifications[*options.snapped]);
        if (snapped_point_value >= closeset_point_value)
        {
            const Vec2d position{positions.at(*options.snapped)};
            return {*options.snapped, *options.snapped, position};
        }
    }

    unsigned adjacent_point_value = get_point_value(types.at(options.adjacent), classifications[options.adjacent]);
    if (adjacent_point_value < closeset_point_value)
    {
        const Vec2d position = positions[options.closest];
        return {options.closest, options.closest, position};
    }

    const std::size_t next_index{options.adjacent_forward ? options.adjacent : options.closest};
    const std::size_t previous_index{options.adjacent_forward ? options.closest : options.adjacent};
    return {previous_index, next_index, options.on_edge};
}

std::optional<std::size_t> snap_to_angle(const Vec2d &point, const std::size_t search_start,
                                         const Perimeters::Perimeter &perimeter, const double max_detour)
{
    using Perimeters::AngleType;
    const std::vector<Vec2d> &positions{perimeter.positions};
    const std::vector<AngleType> &angle_types{perimeter.angle_types};

    std::optional<std::size_t> match;
    double min_distance{std::numeric_limits<double>::infinity()};
    AngleType angle_type{AngleType::convex};

    const auto visitor{[&](const std::size_t index)
                       {
                           const double distance = (positions[index] - point).norm();
                           if (distance > max_detour)
                           {
                               return true;
                           }
                           if (angle_types[index] == angle_type && distance < min_distance)
                           {
                               match = index;
                               min_distance = distance;
                               return true;
                           }
                           return false;
                       }};
    Geometry::visit_backward(search_start, positions.size(), visitor);
    Geometry::visit_forward(search_start, positions.size(), visitor);
    if (match)
    {
        return match;
    }

    min_distance = std::numeric_limits<double>::infinity();
    angle_type = AngleType::concave;

    Geometry::visit_backward(search_start, positions.size(), visitor);
    Geometry::visit_forward(search_start, positions.size(), visitor);

    return match;
}

// Contiguous runs of enforcer vertices by perimeter index, wrap-aware.
std::vector<std::vector<std::size_t>> get_enforcer_runs(const Perimeters::Perimeter &perimeter)
{
    const std::size_t n{perimeter.positions.size()};
    std::vector<std::vector<std::size_t>> runs;
    if (n == 0)
        return runs;

    std::size_t start{n};
    for (std::size_t i = 0; i < n; ++i)
        if (perimeter.point_types[i] != PointType::enforcer)
        {
            start = i;
            break;
        }
    if (start == n)
    {
        // The whole perimeter is painted: one run.
        runs.emplace_back(n);
        std::iota(runs.back().begin(), runs.back().end(), std::size_t{0});
        return runs;
    }

    std::vector<std::size_t> run;
    for (std::size_t k = 0; k < n; ++k)
    {
        const std::size_t i{(start + k) % n};
        if (perimeter.point_types[i] == PointType::enforcer)
        {
            run.push_back(i);
        }
        else if (!run.empty())
        {
            runs.push_back(std::move(run));
            run.clear();
        }
    }
    if (!run.empty())
        runs.push_back(std::move(run));
    return runs;
}

// The seam target on a painted perimeter is the arc-length interpolated midpoint
// of the painted run: always on the wall (the vertex mean sits off the wall on concave arcs
// and its projection amplifies sampling jitter) and continuous under vertex flicker (a
// median vertex steps by one vertex spacing when the run gains or loses a sample, which
// prints as a staircase on a straight painted line). With several painted runs the one
// nearest the reference keeps the column on one region; the distance is not capped, so a
// painted line that moves laterally as the wall rises is followed instead of dropped.
std::optional<Vec2d> get_enforcer_run_midpoint(const Perimeters::Perimeter &perimeter, const Vec2d &reference_position)
{
    const std::vector<std::vector<std::size_t>> runs{get_enforcer_runs(perimeter)};
    if (runs.empty())
        return std::nullopt;

    const std::vector<std::size_t> *best{&runs.front()};
    if (runs.size() > 1)
    {
        double best_distance{std::numeric_limits<double>::infinity()};
        for (const std::vector<std::size_t> &run : runs)
        {
            double run_distance{std::numeric_limits<double>::infinity()};
            for (const std::size_t index : run)
                run_distance = std::min(run_distance, (perimeter.positions[index] - reference_position).norm());
            if (run_distance < best_distance)
            {
                best_distance = run_distance;
                best = &run;
            }
        }
    }

    const std::vector<std::size_t> &run{*best};
    if (run.size() == 1)
        return perimeter.positions[run.front()];
    double total{0.0};
    for (std::size_t i = 0; i + 1 < run.size(); ++i)
        total += (perimeter.positions[run[i + 1]] - perimeter.positions[run[i]]).norm();
    if (total <= 0.0)
        return perimeter.positions[run.front()];
    const double half{total / 2.0};
    double accumulated{0.0};
    for (std::size_t i = 0; i + 1 < run.size(); ++i)
    {
        const Vec2d &a{perimeter.positions[run[i]]};
        const Vec2d &b{perimeter.positions[run[i + 1]]};
        const double segment{(b - a).norm()};
        if (accumulated + segment >= half)
        {
            const double t{segment > 0.0 ? (half - accumulated) / segment : 0.0};
            return Vec2d{a + t * (b - a)};
        }
        accumulated += segment;
    }
    return perimeter.positions[run.back()];
}

std::optional<SeamChoice> choose_painted_seam(const Perimeters::Perimeter &perimeter, const Vec2d &reference_position,
                                              const double max_detour)
{
    const std::optional<Vec2d> midpoint{get_enforcer_run_midpoint(perimeter, reference_position)};
    if (!midpoint)
        return std::nullopt;
    SeamChoice choice{Seams::choose_seam_point(perimeter, Nearest{*midpoint, max_detour})};
    choice.position = *midpoint;
    DBG_COUNT("SEAM_PAINT_CENTERLINE");
    return choice;
}

SeamOptions get_seam_options(const Perimeters::Perimeter &perimeter, const Vec2d &prefered_position,
                             const Perimeters::Perimeter::PointTree &points_tree, const double max_detour)
{
    const std::vector<Vec2d> &positions{perimeter.positions};

    const std::size_t closest{find_closest_point(points_tree, prefered_position.head<2>())};
    std::size_t previous{closest == 0 ? positions.size() - 1 : closest - 1};
    std::size_t next{closest == positions.size() - 1 ? 0 : closest + 1};

    const Vec2d previous_adjacent_point{positions[previous]};
    const Vec2d closest_point{positions[closest]};
    const Vec2d next_adjacent_point{positions[next]};

    const Linef previous_segment{previous_adjacent_point, closest_point};
    const auto [previous_point, previous_distance] = Geometry::distance_to_segment_squared(previous_segment,
                                                                                           prefered_position);
    const Linef next_segment{closest_point, next_adjacent_point};
    const auto [next_point, next_distance] = Geometry::distance_to_segment_squared(next_segment, prefered_position);

    const bool adjacent_forward{next_distance < previous_distance};
    const Vec2d nearest_point{adjacent_forward ? next_point : previous_point};
    const std::size_t adjacent{adjacent_forward ? next : previous};

    std::optional<std::size_t> snapped{snap_to_angle(nearest_point.head<2>(), closest, perimeter, max_detour)};

    return {
        closest, adjacent, adjacent_forward, snapped, nearest_point,
    };
}

// Solve the symmetric positive-definite pentadiagonal system arising from the
// trend filter's normal equations, in place (LU without pivoting).
std::vector<double> solve_pentadiagonal(std::vector<double> a, std::vector<double> b, std::vector<double> c,
                                        std::vector<double> rhs)
{
    const std::size_t n{rhs.size()};
    std::vector<double> l1(n, 0.0), l2(n, 0.0);
    for (std::size_t i = 0; i < n; ++i)
    {
        if (i >= 1)
        {
            const double f{l1[i - 1]};
            a[i] -= f * b[i - 1];
            if (i < n - 1)
                b[i] -= f * c[i - 1];
            rhs[i] -= f * rhs[i - 1];
        }
        if (i >= 2)
        {
            const double f{l2[i - 2]};
            a[i] -= f * c[i - 2];
            rhs[i] -= f * rhs[i - 2];
        }
        if (i < n - 1)
            l1[i] = b[i] / a[i];
        if (i < n - 2)
            l2[i] = c[i] / a[i];
    }
    std::vector<double> out(n, 0.0);
    for (std::size_t i = n; i-- > 0;)
    {
        double v{rhs[i]};
        if (i < n - 1)
            v -= b[i] * out[i + 1];
        if (i < n - 2)
            v -= c[i] * out[i + 2];
        out[i] = v / a[i];
    }
    return out;
}

// L1 trend filtering (iteratively reweighted least squares on the
// second-difference penalty): the output is piecewise linear with slope changes only
// where the data sustains one. Painted-centerline oscillation (a wavy stroke, sampling
// wander on a curved wall) flattens onto a straight line; a genuine bend (the wall
// tapering into a dome) is followed with a knee. This is the "snap a line" rule made
// robust: straight where the paint intends straight, bending only where the object bends.
std::vector<double> l1_trend_filter(const std::vector<double> &values)
{
    constexpr double lambda = 200.0;
    constexpr int iterations = 8;
    constexpr double epsilon = 1e-4;
    const std::size_t n{values.size()};
    if (n < 5)
        return values;

    std::vector<double> weights(n - 2, 1.0);
    std::vector<double> fitted{values};
    for (int it = 0; it < iterations; ++it)
    {
        std::vector<double> a(n, 1.0), b(n, 0.0), c(n, 0.0);
        for (std::size_t i = 0; i < n - 2; ++i)
        {
            const double lw{lambda * weights[i]};
            a[i] += lw;
            a[i + 1] += 4.0 * lw;
            a[i + 2] += lw;
            b[i] += -2.0 * lw;
            b[i + 1] += -2.0 * lw;
            c[i] += lw;
        }
        fitted = solve_pentadiagonal(std::move(a), std::move(b), std::move(c), values);
        for (std::size_t i = 0; i < n - 2; ++i)
            weights[i] = 1.0 / (std::abs(fitted[i] - 2.0 * fitted[i + 1] + fitted[i + 2]) + epsilon);
    }
    return fitted;
}

// The painted seam positions of a shell: the run midpoints of all painted
// layers, trend-filtered per axis. Paint-free layers return nullopt and hold the column
// via the tracker.
std::vector<std::optional<Vec2d>> get_snapped_line_positions(const Shells::Shell<> &shell)
{
    const std::size_t n{shell.size()};
    std::vector<std::optional<Vec2d>> snapped(n);

    std::vector<std::optional<Vec2d>> midpoints(n);
    {
        std::optional<Vec2d> previous;
        for (std::size_t k = 0; k < n; ++k)
        {
            const Perimeters::Perimeter &perimeter{shell[k].boundary};
            const Vec2d reference{previous ? *previous : Vec2d{perimeter.positions.front()}};
            midpoints[k] = get_enforcer_run_midpoint(perimeter, reference);
            if (midpoints[k])
                previous = midpoints[k];
        }
    }

    std::vector<std::size_t> painted;
    for (std::size_t k = 0; k < n; ++k)
        if (midpoints[k])
            painted.push_back(k);
    if (painted.size() < 5)
    {
        for (const std::size_t k : painted)
            snapped[k] = midpoints[k];
        return snapped;
    }

    std::vector<double> series_x(painted.size()), series_y(painted.size());
    for (std::size_t j = 0; j < painted.size(); ++j)
    {
        series_x[j] = (*midpoints[painted[j]]).x();
        series_y[j] = (*midpoints[painted[j]]).y();
    }
    const std::vector<double> fitted_x{l1_trend_filter(series_x)};
    const std::vector<double> fitted_y{l1_trend_filter(series_y)};
    for (std::size_t j = 0; j < painted.size(); ++j)
    {
        snapped[painted[j]] = Vec2d{fitted_x[j], fitted_y[j]};
        DBG_COUNT("SEAM_PAINT_SMOOTHED");
    }
    return snapped;
}

std::optional<SeamChoice> LeastVisible::operator()(const Perimeters::Perimeter &perimeter, const PointType point_type,
                                                   const PointClassification point_classification) const
{
    std::optional<size_t> chosen_index;
    double visibility{std::numeric_limits<double>::infinity()};

    for (std::size_t i{0}; i < perimeter.positions.size(); ++i)
    {
        if (perimeter.point_types[i] != point_type || perimeter.point_classifications[i] != point_classification)
        {
            continue;
        }
        const Vec2d point{perimeter.positions[i]};
        const double point_visibility{precalculated_visibility[i]};

        if (point_visibility < visibility)
        {
            visibility = point_visibility;
            chosen_index = i;
        }
    }

    if (chosen_index)
    {
        return {{*chosen_index, *chosen_index, perimeter.positions[*chosen_index]}};
    }
    return std::nullopt;
}

std::optional<SeamChoice> Nearest::operator()(const Perimeters::Perimeter &perimeter, const PointType point_type,
                                              const PointClassification point_classification) const
{
    const Perimeters::Perimeter::PointTrees &trees{pick_trees(perimeter, point_type)};
    const Perimeters::Perimeter::OptionalPointTree &tree = pick_tree(trees, point_classification);
    if (tree)
    {
        const SeamOptions options{get_seam_options(perimeter, prefered_position, *tree, max_detour)};
        return pick_seam_option(perimeter, options);
    }
    return std::nullopt;
}
} // namespace Impl

double VisibilityCalculator::operator()(const SeamChoice &choice, const Perimeters::Perimeter &perimeter) const
{
    double visibility = points_visibility.calculate_point_visibility(
        to_3d(choice.position, perimeter.slice_z).cast<float>());

    const double angle{choice.previous_index == choice.next_index ? perimeter.angles[choice.previous_index] : 0.0};
    visibility += get_angle_visibility_modifier(angle, convex_visibility_modifier, concave_visibility_modifier);
    return visibility;
}

double VisibilityCalculator::get_angle_visibility_modifier(double angle, const double convex_visibility_modifier,
                                                           const double concave_visibility_modifier)
{
    const double weight_max{angle > 0 ? convex_visibility_modifier : concave_visibility_modifier};
    angle = std::abs(angle);
    const double right_angle{M_PI / 2.0};
    if (angle > right_angle)
    {
        return -weight_max;
    }
    const double angle_linear_weight{angle / right_angle};
    // It is smooth and at angle 0 slope is equal to `angle linear weight`, at right angle the slope is 0 and value is equal to weight max.
    const double angle_smooth_weight{angle / right_angle * weight_max +
                                     (right_angle - angle) / right_angle * angle_linear_weight};
    return -angle_smooth_weight;
}

std::vector<Vec2d> get_starting_positions(const Shells::Shell<> &shell)
{
    const Perimeters::Perimeter &perimeter{shell.front().boundary};

    std::vector<Vec2d> enforcers{Perimeters::extract_points(perimeter, Perimeters::PointType::enforcer)};
    if (!enforcers.empty())
    {
        // One starting position per painted run, at its arc-length midpoint.
        std::vector<Vec2d> starts;
        for (const std::vector<std::size_t> &run : Impl::get_enforcer_runs(perimeter))
            if (auto midpoint = Impl::get_enforcer_run_midpoint(perimeter, perimeter.positions[run.front()]))
                starts.push_back(*midpoint);
        return starts;
    }
    std::vector<Vec2d> common{Perimeters::extract_points(perimeter, Perimeters::PointType::common)};
    if (!common.empty())
    {
        return common;
    }
    return perimeter.positions;
}

struct LeastVisiblePoint
{
    SeamChoice choice;
    double visibility;
};

struct SeamCandidate
{
    std::vector<SeamChoice> choices;
    std::vector<double> visibilities;
};

std::vector<SeamChoice> get_shell_seam(
    const Shells::Shell<> &shell, const std::function<SeamChoice(const Perimeters::Perimeter &, std::size_t)> &chooser)
{
    std::vector<SeamChoice> result;
    result.reserve(shell.size());
    for (std::size_t i{0}; i < shell.size(); ++i)
    {
        const Shells::Slice<> &slice{shell[i]};
        if (slice.boundary.is_degenerate)
        {
            if (std::optional<SeamChoice> seam_choice{choose_degenerate_seam_point(slice.boundary)})
            {
                result.push_back(*seam_choice);
            }
            else
            {
                result.emplace_back();
            }
        }
        else
        {
            const SeamChoice choice{chooser(slice.boundary, i)};
            result.push_back(choice);
        }
    }
    return result;
}

SeamCandidate get_seam_candidate(const Shells::Shell<> &shell, const Vec2d &starting_position,
                                 const SeamChoiceVisibility &visibility_calculator, const Params &params,
                                 const std::vector<std::vector<double>> &precalculated_visibility,
                                 const std::vector<LeastVisiblePoint> &least_visible_points)
{
    using Perimeters::Perimeter, Perimeters::AngleType;

    std::vector<double> choice_visibilities(shell.size(), 1.0);
    const std::vector<std::optional<Vec2d>> snapped_line{Impl::get_snapped_line_positions(shell)};
    std::vector<SeamChoice> choices{get_shell_seam(
        shell,
        // On painted perimeters the seam is the paint centerline: the snapped
        // straight line where the freehand regularization holds (get_snapped_line_positions),
        // otherwise the centerline followed by an alpha-beta tracker: the position predicts
        // forward with a per-layer velocity, so a painted line at a constant lean is followed
        // with zero steady-state lag (a plain low-pass stick-slips there), while the
        // centerline estimator's sampling wiggle is still smoothed. The first two painted
        // layers seed the position and velocity.
        [&, reference_position{starting_position}, velocity{Vec2d{0.0, 0.0}}, warm{0}](const Perimeter &perimeter,
                                                                                       std::size_t slice_index) mutable
        {
            constexpr double tracker_position_gain = 0.4;
            constexpr double tracker_velocity_gain = 0.1;

            if (snapped_line[slice_index])
            {
                const Vec2d snapped_position{*snapped_line[slice_index]};
                SeamChoice snapped_choice{
                    Seams::choose_seam_point(perimeter, Impl::Nearest{snapped_position, params.max_detour})};
                snapped_choice.position = snapped_position;
                choice_visibilities[slice_index] = visibility_calculator(snapped_choice, perimeter);
                reference_position = snapped_position;
                velocity = Vec2d::Zero();
                warm = 2;
                return snapped_choice;
            }

            // The target on a painted perimeter is the arc-length midpoint of the
            // painted run, found on the current perimeter without a distance cap, so a painted
            // line that shifts laterally as the wall rises is followed instead of dropped.
            Vec2d search_target = reference_position;
            bool has_nearby_enforcers = false;
            if (auto run_midpoint = Impl::get_enforcer_run_midpoint(perimeter, reference_position))
            {
                search_target = *run_midpoint;
                has_nearby_enforcers = true;
                if (warm < 2)
                {
                    if (warm == 1)
                        velocity = search_target - reference_position;
                    reference_position = search_target;
                    ++warm;
                }
                else
                {
                    const Vec2d predicted = reference_position + velocity;
                    const Vec2d residual = search_target - predicted;
                    reference_position = predicted + tracker_position_gain * residual;
                    velocity += tracker_velocity_gain * residual;
                }
                search_target = reference_position;
            }
            else
            {
                // A paint-free slice inside a painted shell holds the column, drifting with
                // the learned lean and bleeding the velocity off.
                if (warm > 0)
                {
                    reference_position += velocity;
                    velocity *= 0.5;
                    search_target = reference_position;
                }
            }

            SeamChoice candidate{Seams::choose_seam_point(perimeter, Impl::Nearest{search_target, params.max_detour})};
            // Paint dominates. The detour and visibility fallbacks only apply on
            // paint-free perimeters; a painted run is followed wherever it goes.
            const bool is_too_far{!has_nearby_enforcers &&
                                  (candidate.position - reference_position).norm() > params.max_detour};
            const LeastVisiblePoint &least_visible{least_visible_points[slice_index]};

            const bool is_on_edge{candidate.previous_index == candidate.next_index &&
                                  perimeter.angle_types[candidate.next_index] != AngleType::smooth};

            if (is_on_edge)
            {
                choice_visibilities[slice_index] = precalculated_visibility[slice_index][candidate.previous_index];
            }
            else
            {
                choice_visibilities[slice_index] = visibility_calculator(candidate, perimeter);
            }
            const bool is_too_visible{!has_nearby_enforcers &&
                                      choice_visibilities[slice_index] >
                                          least_visible.visibility + params.jump_visibility_threshold};
            const bool can_be_on_edge{perimeter.angle_types[least_visible.choice.next_index] != AngleType::smooth};
            if (is_too_far || (can_be_on_edge && is_too_visible))
            {
                candidate = least_visible.choice;
                // Update the reference when jumping to least-visible, where the geometry differs a lot
                reference_position = candidate.position;
                velocity = Vec2d::Zero();
                warm = 0;
            }
            else if (has_nearby_enforcers && !is_on_edge)
            {
                // The tracked centerline is the seam.
                candidate.position = reference_position;
            }
            return candidate;
        })};
    // There is no backward smoothing pass: median-run seeding starts the forward pass on the
    // painted run, so the pass has no convergence lag to compensate for, and averaging painted
    // perimeters flattens a genuinely curved painted track toward a chord.

    return {std::move(choices), std::move(choice_visibilities)};
}

using ShellVertexVisibility = std::vector<std::vector<double>>;

std::vector<ShellVertexVisibility> get_shells_vertex_visibility(const Shells::Shells<> &shells,
                                                                const SeamChoiceVisibility &visibility_calculator)
{
    std::vector<ShellVertexVisibility> result;

    result.reserve(shells.size());
    std::transform(shells.begin(), shells.end(), std::back_inserter(result),
                   [](const Shells::Shell<> &shell) { return ShellVertexVisibility(shell.size()); });

    Geometry::iterate_nested(shells,
                             [&](const std::size_t shell_index, const std::size_t slice_index)
                             {
                                 const Shells::Shell<> &shell{shells[shell_index]};
                                 const Shells::Slice<> &slice{shell[slice_index]};
                                 const std::vector<Vec2d> &positions{slice.boundary.positions};

                                 for (std::size_t point_index{0}; point_index < positions.size(); ++point_index)
                                 {
                                     result[shell_index][slice_index].emplace_back(visibility_calculator(
                                         SeamChoice{point_index, point_index, positions[point_index]}, slice.boundary));
                                 }
                             });
    return result;
}

using ShellLeastVisiblePoints = std::vector<LeastVisiblePoint>;

std::vector<ShellLeastVisiblePoints> get_shells_least_visible_points(
    const Shells::Shells<> &shells, const std::vector<ShellVertexVisibility> &precalculated_visibility)
{
    std::vector<ShellLeastVisiblePoints> result;

    result.reserve(shells.size());
    std::transform(shells.begin(), shells.end(), std::back_inserter(result),
                   [](const Shells::Shell<> &shell) { return ShellLeastVisiblePoints(shell.size()); });

    Geometry::iterate_nested(
        shells,
        [&](const std::size_t shell_index, const std::size_t slice_index)
        {
            const Shells::Shell<> &shell{shells[shell_index]};
            const Shells::Slice<> &slice{shell[slice_index]};
            const SeamChoice least_visibile{
                Seams::choose_seam_point(slice.boundary,
                                         Impl::LeastVisible{precalculated_visibility[shell_index][slice_index]})};

            const double visibility{precalculated_visibility[shell_index][slice_index][least_visibile.previous_index]};
            result[shell_index][slice_index] = LeastVisiblePoint{least_visibile, visibility};
        });
    return result;
}

using ShellStartingPositions = std::vector<Vec2d>;

std::vector<ShellStartingPositions> get_shells_starting_positions(const Shells::Shells<> &shells, const Params &params)
{
    std::vector<ShellStartingPositions> result;
    for (const Shells::Shell<> &shell : shells)
    {
        std::vector<Vec2d> starting_positions{get_starting_positions(shell)};
        result.push_back(std::move(starting_positions));
    }
    return result;
}

using ShellSeamCandidates = std::vector<SeamCandidate>;

std::vector<ShellSeamCandidates> get_shells_seam_candidates(
    const Shells::Shells<> &shells, const std::vector<ShellStartingPositions> &starting_positions,
    const SeamChoiceVisibility &visibility_calculator,
    const std::vector<ShellVertexVisibility> &precalculated_visibility,
    const std::vector<ShellLeastVisiblePoints> &least_visible_points, const Params &params)
{
    std::vector<ShellSeamCandidates> result;

    result.reserve(starting_positions.size());
    std::transform(starting_positions.begin(), starting_positions.end(), std::back_inserter(result),
                   [](const ShellStartingPositions &positions) { return ShellSeamCandidates(positions.size()); });

    Geometry::iterate_nested(starting_positions,
                             [&](const std::size_t shell_index, const std::size_t starting_position_index)
                             {
                                 const Shells::Shell<> &shell{shells[shell_index]};
                                 using Perimeters::Perimeter, Perimeters::AngleType;

                                 result[shell_index][starting_position_index] = get_seam_candidate(
                                     shell, starting_positions[shell_index][starting_position_index],
                                     visibility_calculator, params, precalculated_visibility[shell_index],
                                     least_visible_points[shell_index]);
                             });
    return result;
}

std::vector<SeamChoice> get_shell_seam(const Shells::Shell<> &shell, std::vector<SeamCandidate> seam_candidates,
                                       const Perimeters::Perimeter::OptionalPointTree &previous_points,
                                       const Params &params)
{
    std::vector<SeamChoice> seam;
    double visibility{std::numeric_limits<double>::infinity()};

    for (std::size_t i{0}; i < seam_candidates.size(); ++i)
    {
        using Perimeters::Perimeter, Perimeters::AngleType;

        SeamCandidate seam_candidate{seam_candidates[i]};
        const Vec2d first_point{seam_candidate.choices.front().position};

        std::optional<Vec2d> closest_point;
        if (previous_points)
        {
            std::size_t closest_point_index{find_closest_point(*previous_points, first_point)};
            Vec2d point;
            point.x() = previous_points->coordinate(closest_point_index, 0);
            point.y() = previous_points->coordinate(closest_point_index, 1);
            closest_point = point;
        }

        std::optional<double> previous_distance;
        if (closest_point)
        {
            previous_distance = (*closest_point - first_point).norm();
        }
        const bool is_near_previous{closest_point && *previous_distance < params.max_detour};

        double seam_candidate_visibility{is_near_previous
                                             ? -params.continuity_modifier * (params.max_detour - *previous_distance) /
                                                   params.max_detour
                                             : 0.0};
        for (std::size_t slice_index{}; slice_index < shell.size(); ++slice_index)
        {
            seam_candidate_visibility += seam_candidate.visibilities[slice_index];
        }

        if (seam_candidate_visibility < visibility)
        {
            seam = std::move(seam_candidate.choices);
            visibility = seam_candidate_visibility;
        }
    }

    return seam;
}

std::vector<std::vector<SeamPerimeterChoice>> get_object_seams(Shells::Shells<> &&shells,
                                                               const SeamChoiceVisibility &visibility_calculator,
                                                               const Params &params)
{
    const std::vector<ShellVertexVisibility> precalculated_visibility{
        get_shells_vertex_visibility(shells, visibility_calculator)};

    const std::vector<ShellLeastVisiblePoints> least_visible_points{
        get_shells_least_visible_points(shells, precalculated_visibility)};

    const std::vector<ShellStartingPositions> starting_positions{get_shells_starting_positions(shells, params)};

    const std::vector<ShellSeamCandidates> seam_candidates{
        get_shells_seam_candidates(shells, starting_positions, visibility_calculator, precalculated_visibility,
                                   least_visible_points, params)};

    std::vector<std::vector<SeamPerimeterChoice>> layer_seams(get_layer_count(shells));

    for (std::size_t shell_index{0}; shell_index < shells.size(); ++shell_index)
    {
        Shells::Shell<> &shell{shells[shell_index]};

        if (shell.empty())
        {
            continue;
        }

        const std::size_t layer_index{shell.front().layer_index};
        tcb::span<const SeamPerimeterChoice> previous_seams{layer_index == 0 ? tcb::span<const SeamPerimeterChoice>{}
                                                                             : layer_seams[layer_index - 1]};
        std::vector<Vec2d> previous_seams_positions;
        std::transform(previous_seams.begin(), previous_seams.end(), std::back_inserter(previous_seams_positions),
                       [](const SeamPerimeterChoice &seam) { return seam.choice.position; });

        Perimeters::Perimeter::OptionalPointTree previous_seams_positions_tree;
        const Perimeters::Perimeter::IndexToCoord index_to_coord{previous_seams_positions};
        if (!previous_seams_positions.empty())
        {
            previous_seams_positions_tree = Perimeters::Perimeter::PointTree{index_to_coord,
                                                                             index_to_coord.positions.size()};
        }

        std::vector<SeamChoice> seam{
            Aligned::get_shell_seam(shell, seam_candidates[shell_index], previous_seams_positions_tree, params)};

        for (std::size_t perimeter_id{}; perimeter_id < shell.size(); ++perimeter_id)
        {
            const SeamChoice &choice{seam[perimeter_id]};
            Perimeters::Perimeter &perimeter{shell[perimeter_id].boundary};
            layer_seams[shell[perimeter_id].layer_index].emplace_back(choice, std::move(perimeter));
        }
    }
    return layer_seams;
}

std::vector<PaintedColumn> get_painted_columns(const Shells::Shells<> &shells)
{
    std::vector<PaintedColumn> columns;
    columns.reserve(shells.size());
    for (const Shells::Shell<> &shell : shells)
    {
        PaintedColumn column{Impl::get_snapped_line_positions(shell)};
        // A paint-free slice between two painted ones lies on the line between them.
        std::optional<std::size_t> previous;
        for (std::size_t k = 0; k < column.size(); ++k)
        {
            if (!column[k])
                continue;
            if (previous && k > *previous + 1)
            {
                const Vec2d &a{*column[*previous]};
                const Vec2d &b{*column[k]};
                for (std::size_t gap = *previous + 1; gap < k; ++gap)
                {
                    const double t{double(gap - *previous) / double(k - *previous)};
                    column[gap] = a + t * (b - a);
                }
            }
            previous = k;
        }
        columns.push_back(std::move(column));
    }
    return columns;
}

SeamChoice choose_column_seam(const Perimeters::Perimeter &perimeter, const Vec2d &column, const double max_detour)
{
    SeamChoice choice{Seams::choose_seam_point(perimeter, Impl::Nearest{column, max_detour})};
    choice.position = column;
    DBG_COUNT("SEAM_PAINT_CENTERLINE");
    return choice;
}

} // namespace Luminary::Seams::Aligned
