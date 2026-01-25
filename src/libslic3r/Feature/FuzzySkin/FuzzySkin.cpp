///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include <random>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Algorithm/LineSegmentation/LineSegmentation.hpp"
#include "libslic3r/Arachne/utils/ExtrusionJunction.hpp"
#include "libslic3r/Arachne/utils/ExtrusionLine.hpp"
#include "libslic3r/Athena/utils/ExtrusionJunction.hpp"
#include "libslic3r/Athena/utils/ExtrusionLine.hpp"
#include "libslic3r/PerimeterGenerator.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/ClipperUtils.hpp"

#include "FuzzySkin.hpp"
#include "ExtrusionLineSplits.hpp"

#include <thread>
#include <unordered_map>
#include <mutex>

using namespace Slic3r;

namespace Slic3r::Feature::FuzzySkin
{

// When processing external perimeter (idx 0), we track:
// - had_transitions: whether visibility changed along the perimeter
// - all_visible: if no transitions, whether the whole perimeter was visible
// Inner perimeters can skip expensive visibility checking if outer had no transitions.
struct VisibilityState
{
    bool computed = false;        // Whether outer perimeter was processed
    bool had_transitions = false; // Whether visibility changed along perimeter
    bool all_visible = false;     // If no transitions, was it all visible (skip fuzzy)?
};

// Thread-local cache: layer_id -> visibility state
// Using thread_local ensures thread safety without locks for the common case
thread_local std::unordered_map<size_t, VisibilityState> t_visibility_cache;

// Clear cache for a layer (call when starting a new layer)
static void clear_visibility_cache(size_t layer_idx)
{
    t_visibility_cache.erase(layer_idx);
}

// Store visibility result from outer perimeter
static void set_visibility_state(size_t layer_idx, bool had_transitions, bool all_visible)
{
    t_visibility_cache[layer_idx] = {true, had_transitions, all_visible};
}

// Get cached visibility state, returns nullptr if not computed
static const VisibilityState *get_visibility_state(size_t layer_idx)
{
    auto it = t_visibility_cache.find(layer_idx);
    return (it != t_visibility_cache.end() && it->second.computed) ? &it->second : nullptr;
}

void clear_visibility_cache()
{
    t_visibility_cache.clear();
}

// Produces a random value between 0 and 1. Thread-safe.
static double random_value()
{
    thread_local std::random_device rd;
    // Hash thread ID for random number seed if no hardware rng seed is available
    thread_local std::mt19937 gen(rd.entropy() > 0 ? rd() : std::hash<std::thread::id>()(std::this_thread::get_id()));
    thread_local std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(gen);
}

static double lerp(double a, double b, double t)
{
    return a + t * (b - a);
}

FuzzySkinConfig make_fuzzy_config(const PrintRegionConfig &config)
{
    FuzzySkinConfig cfg;
    cfg.type = config.fuzzy_skin.value;
    cfg.thickness = scaled<double>(config.fuzzy_skin_thickness.value);
    cfg.point_distance = scaled<double>(config.fuzzy_skin_point_dist.value);
    cfg.first_layer = config.fuzzy_skin_first_layer.value;
    cfg.on_top = config.fuzzy_skin_on_top.value;
    cfg.noise_type = config.fuzzy_skin_noise_type.value;
    cfg.mode = config.fuzzy_skin_mode.value;
    cfg.scale = config.fuzzy_skin_scale.value;
    cfg.octaves = config.fuzzy_skin_octaves.value;
    cfg.persistence = config.fuzzy_skin_persistence.value;
    cfg.point_placement = config.fuzzy_skin_point_placement.value;
    switch (config.fuzzy_skin_visibility_detection.value)
    {
    case FuzzySkinVisibilityDetection::fsvdPrecise:
        cfg.visibility_detection_interval = 1.0;
        break;
    case FuzzySkinVisibilityDetection::fsvdStandard:
        cfg.visibility_detection_interval = 2.0;
        break;
    case FuzzySkinVisibilityDetection::fsvdRelaxed:
        cfg.visibility_detection_interval = 4.0;
        break;
    case FuzzySkinVisibilityDetection::fsvdMinimal:
        cfg.visibility_detection_interval = 8.0;
        break;
    }
    // max_perimeter_idx is set separately when processing painted segments,
    // NOT here in make_fuzzy_config. This ensures global fuzzy skin uses the
    // fuzzy_skin type setting (External/All/AllWalls) and is not affected by
    // the painted perimeters dropdown.
    return cfg;
}

// Set max_perimeter_idx for painted segments only
// This should be called when processing painted segments to limit fuzzy skin depth
void set_painted_perimeter_limit(FuzzySkinConfig &cfg, const PrintRegionConfig &config)
{
    switch (config.fuzzy_skin_painted_perimeters.value)
    {
    case FuzzySkinPaintedPerimeters::External:
        cfg.max_perimeter_idx = 0;
        break;
    case FuzzySkinPaintedPerimeters::ExternalPlus1:
        cfg.max_perimeter_idx = 1;
        break;
    case FuzzySkinPaintedPerimeters::ExternalPlus2:
        cfg.max_perimeter_idx = 2;
        break;
    case FuzzySkinPaintedPerimeters::ExternalPlus3:
        cfg.max_perimeter_idx = 3;
        break;
    case FuzzySkinPaintedPerimeters::All:
        // Use actual perimeter count - 1 to get all perimeters (0 to N-1)
        cfg.max_perimeter_idx = std::max(0, config.perimeters.value - 1);
        break;
    }
}

// Legacy fuzzy_polyline implementation (random noise only)
void fuzzy_polyline(Points &poly, const bool closed, const double fuzzy_skin_thickness,
                    const double fuzzy_skin_point_distance)
{
    if (poly.size() < 2)
        return;

    const double min_dist_between_points =
        fuzzy_skin_point_distance * 3. /
        4.; // hardcoded: the point distance may vary between 3/4 and 5/4 the supplied value
    const double range_random_point_dist = fuzzy_skin_point_distance / 2.;
    double dist_left_over = random_value() *
                            (min_dist_between_points /
                             2.); // the distance to be traversed on the line before making the first new point

    Points out;
    out.reserve(poly.size());

    // Skip the first point for open polyline.
    Point *p0 = closed ? &poly.back() : &poly.front();
    for (auto it_pt1 = closed ? poly.begin() : std::next(poly.begin()); it_pt1 != poly.end(); ++it_pt1)
    {
        Point &p1 = *it_pt1;

        // 'a' is the (next) new point between p0 and p1
        Vec2d p0p1 = (p1 - *p0).cast<double>();
        double p0p1_size = p0p1.norm();
        double p0pa_dist = dist_left_over;
        for (; p0pa_dist < p0p1_size; p0pa_dist += min_dist_between_points + random_value() * range_random_point_dist)
        {
            double r = random_value() * (fuzzy_skin_thickness * 2.) - fuzzy_skin_thickness;
            out.emplace_back(
                *p0 + (p0p1 * (p0pa_dist / p0p1_size) + perp(p0p1).cast<double>().normalized() * r).cast<coord_t>());
        }

        dist_left_over = p0pa_dist - p0p1_size;
        p0 = &p1;
    }

    while (out.size() < 3)
    {
        size_t point_idx = poly.size() - 2;
        out.emplace_back(poly[point_idx]);
        if (point_idx == 0)
        {
            break;
        }

        --point_idx;
    }

    if (out.size() >= 3)
    {
        poly = std::move(out);
    }
}

// Shape-following fuzzy_polyline implementation
// This algorithm preserves corner vertices for better accuracy at larger point distances
static void fuzzy_polyline_shape_following(Points &points, const bool closed, const double fuzzy_skin_thickness,
                                           const double fuzzy_skin_point_dist)
{
    if (points.size() < 2)
        return;

    Points out;

    const double line_unit_length = 2. / 3. * fuzzy_skin_point_dist;
    const double point_min_delta = 2e-1 * line_unit_length;
    const int n_point = static_cast<int>(points.size());
    int n_seg = n_point;

    // Reduce segments by 1 for open lines or pre-closed loops
    if (!closed || (closed && (points[0] == points[n_seg - 1])))
        --n_seg;

    double total_length = 0;
    for (int i = 0; i < n_seg; ++i)
    {
        total_length += (points[(i + 1) % n_point] - points[i]).cast<double>().norm();
    }

    out.reserve(n_seg + static_cast<size_t>(std::ceil(total_length / line_unit_length)));

    // Fuzzification loop variable initialization
    Vec2d seg_dir;
    Vec2d seg_perp = closed ? perp((points[0] - points[(n_seg - 1 + n_point) % n_point]).cast<double>().normalized())
                            : perp((points[1] - points[0]).cast<double>().normalized());
    Point p_ref = points[0];

    double x_prev = 0;
    double x_next = total_length < (2. * line_unit_length)
                        ? total_length
                        : line_unit_length +
                              random_value() * std::min(line_unit_length, total_length - 2 * line_unit_length);

    double x_prev_corner = 0;
    double x_next_corner = 0;
    int corner_idx = 0;

    double y_0 = (2. * random_value() - 1.) * fuzzy_skin_thickness;
    double y_prev = y_0;
    double y_next = (2. * random_value() - 1.) * fuzzy_skin_thickness;

    // Fuzzification loop
    while (x_prev < total_length)
    {
        // Add any interim corner points from the original line
        while (x_next_corner <= x_next)
        {
            if (corner_idx == n_seg)
                break;
            double y = lerp(y_prev, y_next, (x_next_corner - x_prev) / (x_next - x_prev));
            Vec2d prev_perp = seg_perp;

            p_ref = points[corner_idx];
            Vec2d seg = (points[(corner_idx + 1) % n_point] - p_ref).cast<double>();
            double seg_length = seg.norm();
            seg_dir = seg.normalized();
            seg_perp = perp(seg_dir);

            Vec2d corner_perp = seg_perp.dot(prev_perp) > -0.99 ? Vec2d((seg_perp + prev_perp).normalized()) : seg_dir;
            out.emplace_back(p_ref + (y * corner_perp).cast<coord_t>());

            x_prev_corner = x_next_corner;
            x_next_corner += seg_length;
            ++corner_idx;
        }
        // Add the next mid-segment fuzzy point
        // Only add if not too close to an existing corner point
        if (!((x_next - x_prev_corner) < point_min_delta || (x_next_corner - x_next) < point_min_delta))
            out.emplace_back(p_ref + ((x_next - x_prev_corner) * seg_dir + y_next * seg_perp).cast<coord_t>());

        x_prev = x_next;
        x_next = x_prev > total_length - (2. * line_unit_length)
                     ? total_length
                     : x_prev + line_unit_length +
                           random_value() * std::min(line_unit_length, total_length - x_prev - 2. * line_unit_length);

        y_prev = y_next;
        y_next = (closed && x_next == total_length) ? y_0 : (2. * random_value() - 1.) * fuzzy_skin_thickness;
    }

    // Add the closing corner
    if (closed)
        out.emplace_back(out[0]);
    else
        out.emplace_back(points[n_seg] + (y_next * seg_perp).cast<coord_t>());

    out.shrink_to_fit();
    points = std::move(out);
}

// Shape-following with structured noise support
static void fuzzy_polyline_shape_following(Points &points, const bool closed, const double slice_z,
                                           const FuzzySkinConfig &cfg)
{
    if (points.size() < 2)
        return;

    std::unique_ptr<NoiseModule> noise = createNoiseModule(cfg);
    Points out;

    const double line_unit_length = 2. / 3. * cfg.point_distance;
    const double point_min_delta = 2e-1 * line_unit_length;
    const int n_point = static_cast<int>(points.size());
    int n_seg = n_point;

    if (!closed || (closed && (points[0] == points[n_seg - 1])))
        --n_seg;

    double total_length = 0;
    for (int i = 0; i < n_seg; ++i)
    {
        total_length += (points[(i + 1) % n_point] - points[i]).cast<double>().norm();
    }

    out.reserve(n_seg + static_cast<size_t>(std::ceil(total_length / line_unit_length)));

    Vec2d seg_dir;
    Vec2d seg_perp = closed ? perp((points[0] - points[(n_seg - 1 + n_point) % n_point]).cast<double>().normalized())
                            : perp((points[1] - points[0]).cast<double>().normalized());
    Point p_ref = points[0];

    double x_prev = 0;
    double x_next = total_length < (2. * line_unit_length)
                        ? total_length
                        : line_unit_length +
                              random_value() * std::min(line_unit_length, total_length - 2 * line_unit_length);

    double x_prev_corner = 0;
    double x_next_corner = 0;
    int corner_idx = 0;

    // Get initial noise values
    double y_0 = noise->getValue(unscale<double>(p_ref.x()), unscale<double>(p_ref.y()), slice_z) * cfg.thickness;
    double y_prev = y_0;
    Point next_sample_pt = p_ref;
    double y_next = noise->getValue(unscale<double>(next_sample_pt.x()), unscale<double>(next_sample_pt.y()), slice_z) *
                    cfg.thickness;

    while (x_prev < total_length)
    {
        while (x_next_corner <= x_next)
        {
            if (corner_idx == n_seg)
                break;
            double y = lerp(y_prev, y_next, (x_next_corner - x_prev) / (x_next - x_prev));
            Vec2d prev_perp = seg_perp;

            p_ref = points[corner_idx];
            Vec2d seg = (points[(corner_idx + 1) % n_point] - p_ref).cast<double>();
            double seg_length = seg.norm();
            seg_dir = seg.normalized();
            seg_perp = perp(seg_dir);

            Vec2d corner_perp = seg_perp.dot(prev_perp) > -0.99 ? Vec2d((seg_perp + prev_perp).normalized()) : seg_dir;
            out.emplace_back(p_ref + (y * corner_perp).cast<coord_t>());

            x_prev_corner = x_next_corner;
            x_next_corner += seg_length;
            ++corner_idx;
        }

        if (!((x_next - x_prev_corner) < point_min_delta || (x_next_corner - x_next) < point_min_delta))
        {
            Point new_pt = p_ref + ((x_next - x_prev_corner) * seg_dir + y_next * seg_perp).cast<coord_t>();
            out.emplace_back(new_pt);
        }

        x_prev = x_next;
        x_next = x_prev > total_length - (2. * line_unit_length)
                     ? total_length
                     : x_prev + line_unit_length +
                           random_value() * std::min(line_unit_length, total_length - x_prev - 2. * line_unit_length);

        y_prev = y_next;
        // Sample noise at approximate next position
        if (corner_idx < n_seg)
        {
            next_sample_pt = p_ref + ((x_next - x_prev_corner) * seg_dir).cast<coord_t>();
        }
        y_next = (closed && x_next == total_length) ? y_0
                                                    : noise->getValue(unscale<double>(next_sample_pt.x()),
                                                                      unscale<double>(next_sample_pt.y()), slice_z) *
                                                          cfg.thickness;
    }

    if (closed)
        out.emplace_back(out[0]);
    else
    {
        Point final_pt = points[n_seg] + (y_next * seg_perp).cast<coord_t>();
        out.emplace_back(final_pt);
    }

    out.shrink_to_fit();
    points = std::move(out);
}

void fuzzy_polyline(Points &poly, const bool closed, const double slice_z, const FuzzySkinConfig &cfg)
{
    if (poly.size() < 2)
        return;

    if (cfg.point_placement == FuzzySkinPointPlacement::ShapeFollowing)
    {
        fuzzy_polyline_shape_following(poly, closed, slice_z, cfg);
        return;
    }

    std::unique_ptr<NoiseModule> noise = createNoiseModule(cfg);

    const double min_dist_between_points = cfg.point_distance * 3. / 4.;
    const double range_random_point_dist = cfg.point_distance / 2.;
    double dist_left_over = random_value() * (min_dist_between_points / 2.);

    Points out;
    out.reserve(poly.size());

    Point *p0 = closed ? &poly.back() : &poly.front();
    for (auto it_pt1 = closed ? poly.begin() : std::next(poly.begin()); it_pt1 != poly.end(); ++it_pt1)
    {
        Point &p1 = *it_pt1;

        Vec2d p0p1 = (p1 - *p0).cast<double>();
        double p0p1_size = p0p1.norm();
        double p0pa_dist = dist_left_over;
        for (; p0pa_dist < p0p1_size; p0pa_dist += min_dist_between_points + random_value() * range_random_point_dist)
        {
            Point pa = *p0 + (p0p1 * (p0pa_dist / p0p1_size)).cast<coord_t>();
            // Get noise value at this 3D position (convert from scaled to mm for noise sampling)
            double r = noise->getValue(unscale<double>(pa.x()), unscale<double>(pa.y()), slice_z) * cfg.thickness;
            out.emplace_back(pa + (perp(p0p1).cast<double>().normalized() * r).cast<coord_t>());
        }

        dist_left_over = p0pa_dist - p0p1_size;
        p0 = &p1;
    }

    while (out.size() < 3)
    {
        size_t point_idx = poly.size() - 2;
        out.emplace_back(poly[point_idx]);
        if (point_idx == 0)
        {
            break;
        }
        --point_idx;
    }

    if (out.size() >= 3)
    {
        poly = std::move(out);
    }
}

void fuzzy_polygon(Polygon &polygon, double fuzzy_skin_thickness, double fuzzy_skin_point_distance)
{
    fuzzy_polyline(polygon.points, true, fuzzy_skin_thickness, fuzzy_skin_point_distance);
}

void fuzzy_polygon(Polygon &polygon, double slice_z, const FuzzySkinConfig &cfg)
{
    fuzzy_polyline(polygon.points, true, slice_z, cfg);
}

void fuzzy_extrusion_line(Arachne::ExtrusionLine &ext_lines, const double fuzzy_skin_thickness,
                          const double fuzzy_skin_point_distance)
{
    if (ext_lines.size() < 2)
        return;

    const double min_dist_between_points =
        fuzzy_skin_point_distance * 3. /
        4.; // hardcoded: the point distance may vary between 3/4 and 5/4 the supplied value
    const double range_random_point_dist = fuzzy_skin_point_distance / 2.;
    double dist_left_over = random_value() *
                            (min_dist_between_points /
                             2.); // the distance to be traversed on the line before making the first new point

    Arachne::ExtrusionJunction *p0 = &ext_lines.front();
    Arachne::ExtrusionJunctions out;
    out.reserve(ext_lines.size());
    for (auto &p1 : ext_lines)
    {
        if (p0->p == p1.p)
        {
            // Copy the first point.
            out.emplace_back(p1.p, p1.w, p1.perimeter_index);
            continue;
        }

        // 'a' is the (next) new point between p0 and p1
        Vec2d p0p1 = (p1.p - p0->p).cast<double>();
        double p0p1_size = p0p1.norm();
        double p0pa_dist = dist_left_over;
        for (; p0pa_dist < p0p1_size; p0pa_dist += min_dist_between_points + random_value() * range_random_point_dist)
        {
            double r = random_value() * (fuzzy_skin_thickness * 2.) - fuzzy_skin_thickness;
            out.emplace_back(
                p0->p + (p0p1 * (p0pa_dist / p0p1_size) + perp(p0p1).cast<double>().normalized() * r).cast<coord_t>(),
                p1.w, p1.perimeter_index);
        }

        dist_left_over = p0pa_dist - p0p1_size;
        p0 = &p1;
    }

    while (out.size() < 3)
    {
        size_t point_idx = ext_lines.size() - 2;
        out.emplace_back(ext_lines[point_idx].p, ext_lines[point_idx].w, ext_lines[point_idx].perimeter_index);
        if (point_idx == 0)
        {
            break;
        }

        --point_idx;
    }

    if (ext_lines.back().p == ext_lines.front().p)
    {
        // Connect endpoints.
        out.front().p = out.back().p;
    }

    if (out.size() >= 3)
    {
        ext_lines.junctions = std::move(out);
    }
}

// Shape-following Arachne extrusion line
static void fuzzy_extrusion_line_shape_following(Arachne::ExtrusionLine &ext_lines, const double slice_z,
                                                 const FuzzySkinConfig &cfg)
{
    if (ext_lines.size() < 2)
        return;

    const bool closed = ext_lines.is_closed;
    const std::vector<Arachne::ExtrusionJunction> &points = ext_lines.junctions;

    std::unique_ptr<NoiseModule> noise = createNoiseModule(cfg);
    std::vector<Arachne::ExtrusionJunction> out;

    const double line_unit_length = 2. / 3. * cfg.point_distance;
    const double point_min_delta = 2e-1 * line_unit_length;
    const double min_extrusion_width = 0.01;
    const int n_point = static_cast<int>(points.size());
    int n_seg = n_point;

    if (!closed || (closed && (points[0].p == points[n_seg - 1].p)))
        --n_seg;

    double total_length = 0;
    for (int i = 0; i < n_seg; ++i)
    {
        total_length += (points[(i + 1) % n_point].p - points[i].p).cast<double>().norm();
    }

    out.reserve(n_seg + static_cast<size_t>(std::ceil(total_length / line_unit_length)));

    Vec2d seg_dir;
    Vec2d seg_perp = closed
                         ? perp((points[0].p - points[(n_seg - 1 + n_point) % n_point].p).cast<double>().normalized())
                         : perp((points[1].p - points[0].p).cast<double>().normalized());
    Arachne::ExtrusionJunction p_ref = points[0];

    double x_prev = 0;
    double x_next = total_length < (2. * line_unit_length)
                        ? total_length
                        : line_unit_length +
                              random_value() * std::min(line_unit_length, total_length - 2 * line_unit_length);

    double x_prev_corner = 0;
    double x_next_corner = 0;
    int corner_idx = 0;

    double y_0 = noise->getValue(unscale<double>(p_ref.p.x()), unscale<double>(p_ref.p.y()), slice_z) * cfg.thickness;
    double y_prev = y_0;
    Point next_sample_pt = p_ref.p;
    double y_next = noise->getValue(unscale<double>(next_sample_pt.x()), unscale<double>(next_sample_pt.y()), slice_z) *
                    cfg.thickness;

    while (x_prev < total_length)
    {
        while (x_next_corner <= x_next)
        {
            if (corner_idx == n_seg)
                break;
            double y = lerp(y_prev, y_next, (x_next_corner - x_prev) / (x_next - x_prev));
            Vec2d prev_perp = seg_perp;

            p_ref = points[corner_idx];
            Vec2d seg = (points[(corner_idx + 1) % n_point].p - p_ref.p).cast<double>();
            double seg_length = seg.norm();
            seg_dir = seg.normalized();
            seg_perp = perp(seg_dir);

            Vec2d corner_perp = seg_perp.dot(prev_perp) > -0.99 ? Vec2d((seg_perp + prev_perp).normalized()) : seg_dir;

            switch (cfg.mode)
            {
            case FuzzySkinMode::Displacement:
                out.emplace_back(p_ref.p + (y * corner_perp).cast<coord_t>(), p_ref.w, p_ref.perimeter_index);
                break;
            case FuzzySkinMode::Extrusion:
                out.emplace_back(p_ref.p, std::max(p_ref.w + y + min_extrusion_width, min_extrusion_width),
                                 p_ref.perimeter_index);
                break;
            case FuzzySkinMode::Combined:
            {
                double rad = std::max(p_ref.w + y + min_extrusion_width, min_extrusion_width);
                out.emplace_back(p_ref.p + (((rad - p_ref.w) / 2) * corner_perp).cast<coord_t>(), rad,
                                 p_ref.perimeter_index);
                break;
            }
            }

            x_prev_corner = x_next_corner;
            x_next_corner += seg_length;
            ++corner_idx;
        }

        if (!((x_next - x_prev_corner) < point_min_delta || (x_next_corner - x_next) < point_min_delta))
        {
            Point new_pos = p_ref.p + ((x_next - x_prev_corner) * seg_dir + y_next * seg_perp).cast<coord_t>();
            switch (cfg.mode)
            {
            case FuzzySkinMode::Displacement:
                out.emplace_back(new_pos, p_ref.w, p_ref.perimeter_index);
                break;
            case FuzzySkinMode::Extrusion:
            {
                Point base_pos = p_ref.p + ((x_next - x_prev_corner) * seg_dir).cast<coord_t>();
                out.emplace_back(base_pos, std::max(p_ref.w + y_next + min_extrusion_width, min_extrusion_width),
                                 p_ref.perimeter_index);
                break;
            }
            case FuzzySkinMode::Combined:
            {
                double rad = std::max(p_ref.w + y_next + min_extrusion_width, min_extrusion_width);
                Point base_pos = p_ref.p + ((x_next - x_prev_corner) * seg_dir).cast<coord_t>();
                out.emplace_back(base_pos + (((rad - p_ref.w) / 2) * seg_perp).cast<coord_t>(), rad,
                                 p_ref.perimeter_index);
                break;
            }
            }
        }

        x_prev = x_next;
        x_next = x_prev > total_length - (2. * line_unit_length)
                     ? total_length
                     : x_prev + line_unit_length +
                           random_value() * std::min(line_unit_length, total_length - x_prev - 2. * line_unit_length);

        y_prev = y_next;
        if (corner_idx < n_seg)
        {
            next_sample_pt = p_ref.p + ((x_next - x_prev_corner) * seg_dir).cast<coord_t>();
        }
        y_next = (closed && x_next == total_length) ? y_0
                                                    : noise->getValue(unscale<double>(next_sample_pt.x()),
                                                                      unscale<double>(next_sample_pt.y()), slice_z) *
                                                          cfg.thickness;
    }

    if (closed)
        out.emplace_back(out[0]);
    else
        out.emplace_back(points[n_seg].p + (y_next * seg_perp).cast<coord_t>(), p_ref.w, p_ref.perimeter_index);

    out.shrink_to_fit();
    ext_lines.junctions = std::move(out);
}

void fuzzy_extrusion_line(Arachne::ExtrusionLine &ext_lines, const double slice_z, const FuzzySkinConfig &cfg)
{
    if (ext_lines.size() < 2)
        return;

    if (cfg.point_placement == FuzzySkinPointPlacement::ShapeFollowing)
    {
        fuzzy_extrusion_line_shape_following(ext_lines, slice_z, cfg);
        return;
    }

    std::unique_ptr<NoiseModule> noise = createNoiseModule(cfg);

    const double min_dist_between_points = cfg.point_distance * 3. / 4.;
    const double range_random_point_dist = cfg.point_distance / 2.;
    const double min_extrusion_width = 0.01;
    double dist_left_over = random_value() * (min_dist_between_points / 2.);

    Arachne::ExtrusionJunction *p0 = &ext_lines.front();
    Arachne::ExtrusionJunctions out;
    out.reserve(ext_lines.size());

    for (auto &p1 : ext_lines)
    {
        if (p0->p == p1.p)
        {
            out.emplace_back(p1.p, p1.w, p1.perimeter_index);
            continue;
        }

        Vec2d p0p1 = (p1.p - p0->p).cast<double>();
        double p0p1_size = p0p1.norm();
        double p0pa_dist = dist_left_over;

        for (; p0pa_dist < p0p1_size; p0pa_dist += min_dist_between_points + random_value() * range_random_point_dist)
        {
            Point pa = p0->p + (p0p1 * (p0pa_dist / p0p1_size)).cast<coord_t>();
            double r = noise->getValue(unscale<double>(pa.x()), unscale<double>(pa.y()), slice_z) * cfg.thickness;

            switch (cfg.mode)
            {
            case FuzzySkinMode::Displacement:
                out.emplace_back(pa + (perp(p0p1).cast<double>().normalized() * r).cast<coord_t>(), p1.w,
                                 p1.perimeter_index);
                break;
            case FuzzySkinMode::Extrusion:
                out.emplace_back(pa, std::max(p1.w + r + min_extrusion_width, min_extrusion_width), p1.perimeter_index);
                break;
            case FuzzySkinMode::Combined:
            {
                double rad = std::max(p1.w + r + min_extrusion_width, min_extrusion_width);
                out.emplace_back(pa + (perp(p0p1).cast<double>().normalized() * ((rad - p1.w) / 2)).cast<coord_t>(),
                                 rad, p1.perimeter_index);
                break;
            }
            }
        }

        dist_left_over = p0pa_dist - p0p1_size;
        p0 = &p1;
    }

    while (out.size() < 3)
    {
        size_t point_idx = ext_lines.size() - 2;
        out.emplace_back(ext_lines[point_idx].p, ext_lines[point_idx].w, ext_lines[point_idx].perimeter_index);
        if (point_idx == 0)
        {
            break;
        }
        --point_idx;
    }

    if (ext_lines.back().p == ext_lines.front().p)
    {
        out.front().p = out.back().p;
        out.front().w = out.back().w;
    }

    if (out.size() >= 3)
    {
        ext_lines.junctions = std::move(out);
    }
}

bool should_fuzzify(const FuzzySkinConfig &config, const int layer_id, const size_t loop_idx, const bool is_contour)
{
    if (config.type == FuzzySkinType::None)
    {
        return false;
    }

    // When first_layer is false, skip fuzzy on layer 0 for better bed adhesion
    if (layer_id == 0 && !config.first_layer)
    {
        return false;
    }

    // Check max_perimeter_idx for painted regions
    // If max_perimeter_idx is set (>= 0), use it to limit which perimeters get fuzzified
    // This allows "External +1", "External +2", etc. options for painted fuzzy skin
    // Allow BOTH contour AND hole perimeters to be processed - the painted region
    // segmentation will determine which segments are actually fuzzified.
    // This enables painting inside holes to apply fuzzy skin to hole perimeters.
    if (config.max_perimeter_idx >= 0)
    {
        // Allow perimeters (contour or hole) up to the specified depth
        // The painted region intersection will determine actual fuzzy application
        return static_cast<int>(loop_idx) <= config.max_perimeter_idx;
    }

    // Handle all fuzzy skin type options
    // Determine max perimeter depth based on type
    int max_depth = 0;
    bool include_holes = false;

    switch (config.type)
    {
    case FuzzySkinType::None:
        return false;
    // No holes options
    case FuzzySkinType::External:
        max_depth = 0;
        include_holes = false;
        break;
    case FuzzySkinType::ExternalPlus1:
        max_depth = 1;
        include_holes = false;
        break;
    case FuzzySkinType::ExternalPlus2:
        max_depth = 2;
        include_holes = false;
        break;
    case FuzzySkinType::ExternalPlus3:
        max_depth = 3;
        include_holes = false;
        break;
    case FuzzySkinType::All:
        max_depth = 9999; // unlimited
        include_holes = false;
        break;
    // With holes options
    case FuzzySkinType::ExternalWithHoles:
        max_depth = 0;
        include_holes = true;
        break;
    case FuzzySkinType::ExternalPlus1WithHoles:
        max_depth = 1;
        include_holes = true;
        break;
    case FuzzySkinType::ExternalPlus2WithHoles:
        max_depth = 2;
        include_holes = true;
        break;
    case FuzzySkinType::ExternalPlus3WithHoles:
        max_depth = 3;
        include_holes = true;
        break;
    case FuzzySkinType::AllWalls:
        max_depth = 9999; // unlimited
        include_holes = true;
        break;
    }

    // Check perimeter depth
    if (static_cast<int>(loop_idx) > max_depth)
        return false;

    // Check if this is a hole perimeter and whether holes are allowed
    if (!is_contour && !include_holes)
        return false;

    return true;
}

bool should_fuzzify(const PrintRegionConfig &config, const size_t layer_idx, const size_t perimeter_idx,
                    const bool is_contour)
{
    const FuzzySkinType fuzzy_skin_type = config.fuzzy_skin.value;

    if (fuzzy_skin_type == FuzzySkinType::None)
    {
        return false;
    }

    // When fuzzy_skin_first_layer is false, skip fuzzy on layer 0 for better bed adhesion
    if (layer_idx == 0 && !config.fuzzy_skin_first_layer.value)
    {
        return false;
    }

    // Handle all fuzzy skin type options
    // Determine max perimeter depth and hole inclusion based on type
    int max_depth = 0;
    bool include_holes = false;

    switch (fuzzy_skin_type)
    {
    case FuzzySkinType::None:
        return false;
    // No holes options
    case FuzzySkinType::External:
        max_depth = 0;
        include_holes = false;
        break;
    case FuzzySkinType::ExternalPlus1:
        max_depth = 1;
        include_holes = false;
        break;
    case FuzzySkinType::ExternalPlus2:
        max_depth = 2;
        include_holes = false;
        break;
    case FuzzySkinType::ExternalPlus3:
        max_depth = 3;
        include_holes = false;
        break;
    case FuzzySkinType::All:
        max_depth = 9999; // unlimited
        include_holes = false;
        break;
    // With holes options
    case FuzzySkinType::ExternalWithHoles:
        max_depth = 0;
        include_holes = true;
        break;
    case FuzzySkinType::ExternalPlus1WithHoles:
        max_depth = 1;
        include_holes = true;
        break;
    case FuzzySkinType::ExternalPlus2WithHoles:
        max_depth = 2;
        include_holes = true;
        break;
    case FuzzySkinType::ExternalPlus3WithHoles:
        max_depth = 3;
        include_holes = true;
        break;
    case FuzzySkinType::AllWalls:
        max_depth = 9999; // unlimited
        include_holes = true;
        break;
    }

    // Check perimeter depth
    if (static_cast<int>(perimeter_idx) > max_depth)
        return false;

    // Check if this is a hole perimeter and whether holes are allowed
    if (!is_contour && !include_holes)
        return false;

    return true;
}

// Added Layer* parameter for per-segment visibility checks
// Added lower_slices parameter to exclude overhangs
// Added ext_perimeter_width parameter
Polygon apply_fuzzy_skin(const Polygon &polygon, const PrintRegionConfig &base_config,
                         const PerimeterRegions &perimeter_regions, const size_t layer_idx, const size_t perimeter_idx,
                         const bool is_contour, const Layer *layer, const Polygons *lower_slices,
                         const coord_t ext_perimeter_width)
{
    // Legacy version - delegate to new version with slice_z = 0 (uses random noise)
    return apply_fuzzy_skin(polygon, base_config, perimeter_regions, layer_idx, perimeter_idx, is_contour, 0.0, layer,
                            lower_slices, ext_perimeter_width);
}

// New apply_fuzzy_skin with slice_z for structured noise
// Added ext_perimeter_width parameter
Polygon apply_fuzzy_skin(const Polygon &polygon, const PrintRegionConfig &base_config,
                         const PerimeterRegions &perimeter_regions, const size_t layer_idx, const size_t perimeter_idx,
                         const bool is_contour, const double slice_z, const Layer *layer, const Polygons *lower_slices,
                         const coord_t ext_perimeter_width)
{
    using namespace Slic3r::Algorithm::LineSegmentation;

    FuzzySkinConfig cfg = make_fuzzy_config(base_config);

    // Segment splitting at visibility boundaries
    // Split segments at top/bottom visibility boundaries
    // Scale check diameter based on external perimeter width
    // Coarse detection uses 4x perimeter width, fine binary search uses 0.5x
    // No point refining beyond the probe size - that's our measurement resolution
    const coord_t check_diameter_coarse = (ext_perimeter_width > 0) ? (ext_perimeter_width * 4) : scaled(1.6);
    const coord_t check_diameter_fine = (ext_perimeter_width > 0) ? (ext_perimeter_width / 2) : scaled(0.2);

    // Geometric overhang detection
    // A point is an overhang if it falls OUTSIDE the lower_slices polygons.
    // lower_slices is already offset by half nozzle diameter in PerimeterGenerator.
    auto point_is_overhang = [lower_slices](const Point &pt) -> bool
    {
        if (!lower_slices || lower_slices->empty())
            return false;
        for (const Polygon &poly : *lower_slices)
        {
            if (poly.contains(pt))
                return false;
        }
        return true;
    };

    // Find the exact point where a segment crosses the overhang boundary using binary search
    // Returns the crossing point between p1 (inside) and p2 (outside) or vice versa
    auto find_overhang_boundary = [&point_is_overhang](const Point &p1, const Point &p2) -> Point
    {
        Point inside = p1;
        Point outside = p2;
        if (point_is_overhang(p1))
            std::swap(inside, outside);

        // Binary search to find crossing point (precision ~0.01mm)
        for (int i = 0; i < 14; ++i)
        { // 2^14 = 16384, gives sub-micron precision
            Point mid((inside.x() + outside.x()) / 2, (inside.y() + outside.y()) / 2);
            if (point_is_overhang(mid))
            {
                outside = mid;
            }
            else
            {
                inside = mid;
            }
        }
        return Point((inside.x() + outside.x()) / 2, (inside.y() + outside.y()) / 2);
    };

    // Check if a point should skip fuzzy due to visibility from ABOVE only
    // Only check top visibility since overhangs handle below
    // Overhang exclusion now handles "unsupported from below" case, so we only need to check
    // if the point is visible from above (top surface that shouldn't have fuzzy)
    // Parameterized check diameter
    auto point_should_skip_visibility = [layer](const Point &pt, const FuzzySkinConfig &config,
                                                coord_t check_diameter) -> bool
    {
        if (!layer)
            return false;
        if (config.on_top)
            return false; // Fuzzy allowed on top, no need to check
        return layer->is_visible_from_top_or_bottom(pt, check_diameter, true,
                                                    false); // check_top=true, check_bottom=false
    };

    // Structure for split sub-segments
    struct SplitSegment
    {
        Points points;
        bool is_overhang; // true = overhang, never fuzzify
        bool should_skip; // true = skip fuzzy (visible surface or overhang)
    };

    // Split polyline at EXACT overhang boundaries first
    // This uses binary search to find precise crossing points, not interval sampling
    auto split_at_overhang_boundaries = [&point_is_overhang, &find_overhang_boundary,
                                         lower_slices](const Points &points) -> std::vector<SplitSegment>
    {
        std::vector<SplitSegment> result;
        if (!lower_slices || lower_slices->empty() || points.size() < 2)
        {
            // No overhang detection needed
            result.push_back({points, false, false});
            return result;
        }

        bool current_overhang = point_is_overhang(points[0]);
        Points current_segment;
        current_segment.push_back(points[0]);

        for (size_t i = 1; i < points.size(); ++i)
        {
            const Point &prev_pt = points[i - 1];
            const Point &curr_pt = points[i];
            bool curr_overhang = point_is_overhang(curr_pt);

            if (curr_overhang != current_overhang)
            {
                // Overhang state changed - find exact boundary using binary search
                Point boundary = find_overhang_boundary(prev_pt, curr_pt);
                current_segment.push_back(boundary);
                result.push_back({std::move(current_segment), current_overhang, current_overhang});
                current_segment.clear();
                current_segment.push_back(boundary);
                current_overhang = curr_overhang;
            }
            current_segment.push_back(curr_pt);
        }

        if (!current_segment.empty())
        {
            result.push_back({std::move(current_segment), current_overhang, current_overhang});
        }

        return result;
    };

    // Binary search to find exact visibility boundary
    // When visibility changes between two points, binary search to find the precise boundary
    // Refines down to ~0.125mm precision for accurate splits
    // Use fine diameter for precise boundary finding
    auto find_visibility_boundary = [&point_should_skip_visibility, check_diameter_fine,
                                     check_diameter_coarse](const Point &p1, const Point &p2,
                                                            const FuzzySkinConfig &config) -> Point
    {
        // p1 and p2 have different visibility states - find the boundary
        Point visible_pt = p1;
        Point hidden_pt = p2;
        // Use coarse diameter for initial state check (matching detection phase)
        bool p1_skip = point_should_skip_visibility(p1, config, check_diameter_coarse);
        bool p2_skip = point_should_skip_visibility(p2, config, check_diameter_coarse);
        if (p1_skip)
            std::swap(visible_pt, hidden_pt);

        // Binary search to find crossing point - use FINE diameter for precision
        // Stop when interval < fine diameter (no point being more precise than probe size)
        const double min_precision = unscale<double>(check_diameter_fine);
        double distance = unscale<double>((hidden_pt - visible_pt).cast<double>().norm());
        int iterations = 0;

        while (distance > min_precision)
        {
            Point mid((visible_pt.x() + hidden_pt.x()) / 2, (visible_pt.y() + hidden_pt.y()) / 2);
            bool mid_skip = point_should_skip_visibility(mid, config, check_diameter_fine);
            if (mid_skip)
            {
                hidden_pt = mid;
            }
            else
            {
                visible_pt = mid;
            }
            distance = unscale<double>((hidden_pt - visible_pt).cast<double>().norm());
            iterations++;
        }

        Point result((visible_pt.x() + hidden_pt.x()) / 2, (visible_pt.y() + hidden_pt.y()) / 2);
        return result;
    };

    // Split a polyline at visibility boundaries (for non-overhang segments only)
    // Uses interval sampling to detect transitions, then binary search to find exact boundary
    // Capture coarse diameter for sampling phase
    auto split_by_visibility = [&point_should_skip_visibility, &find_visibility_boundary, layer,
                                check_diameter_coarse](const Points &points,
                                                       const FuzzySkinConfig &config) -> std::vector<SplitSegment>
    {
        std::vector<SplitSegment> result;
        if (!layer || points.size() < 2)
        {
            result.push_back({points, false, false});
            return result;
        }

        const double sample_interval = config.visibility_detection_interval;
        bool current_skip = point_should_skip_visibility(points[0], config, check_diameter_coarse);
        Points current_segment;
        current_segment.push_back(points[0]);
        Point last_known_state_pt = points[0]; // Last point with known visibility state

        for (size_t i = 1; i < points.size(); ++i)
        {
            const Point &prev_pt = points[i - 1];
            const Point &curr_pt = points[i];
            double seg_len = unscale<double>((curr_pt - prev_pt).cast<double>().norm());

            if (seg_len <= sample_interval)
            {
                bool end_skip = point_should_skip_visibility(curr_pt, config, check_diameter_coarse);
                if (end_skip != current_skip)
                {
                    // Use binary search for exact boundary
                    Point boundary = find_visibility_boundary(last_known_state_pt, curr_pt, config);
                    current_segment.push_back(boundary);
                    result.push_back({std::move(current_segment), false, current_skip});
                    current_segment.clear();
                    current_segment.push_back(boundary);
                    current_skip = end_skip;
                }
                current_segment.push_back(curr_pt);
                last_known_state_pt = curr_pt;
            }
            else
            {
                Vec2d direction = (curr_pt - prev_pt).cast<double>();
                double dir_len = direction.norm();
                Vec2d dir_unit = direction / dir_len;

                double distance_along = sample_interval;
                while (distance_along < seg_len)
                {
                    Point sample_pt(prev_pt.x() + coord_t(dir_unit.x() * scaled(distance_along)),
                                    prev_pt.y() + coord_t(dir_unit.y() * scaled(distance_along)));
                    bool sample_skip = point_should_skip_visibility(sample_pt, config, check_diameter_coarse);

                    if (sample_skip != current_skip)
                    {
                        // Use binary search for exact boundary
                        Point boundary = find_visibility_boundary(last_known_state_pt, sample_pt, config);
                        current_segment.push_back(boundary);
                        result.push_back({std::move(current_segment), false, current_skip});
                        current_segment.clear();
                        current_segment.push_back(boundary);
                        current_skip = sample_skip;
                    }
                    last_known_state_pt = sample_pt;
                    distance_along += sample_interval;
                }
                bool end_skip = point_should_skip_visibility(curr_pt, config, check_diameter_coarse);
                if (end_skip != current_skip)
                {
                    // Use binary search for exact boundary
                    Point boundary = find_visibility_boundary(last_known_state_pt, curr_pt, config);
                    current_segment.push_back(boundary);
                    result.push_back({std::move(current_segment), false, current_skip});
                    current_segment.clear();
                    current_segment.push_back(boundary);
                    current_skip = end_skip;
                }
                current_segment.push_back(curr_pt);
                last_known_state_pt = curr_pt;
            }
        }

        if (!current_segment.empty())
        {
            result.push_back({std::move(current_segment), false, current_skip});
        }

        return result;
    };

    // Combined split: first at overhang boundaries (precise), then visibility (interval-based)
    auto split_polygon_segments = [&split_at_overhang_boundaries, &split_by_visibility,
                                   layer](const Points &points,
                                          const FuzzySkinConfig &config) -> std::vector<SplitSegment>
    {
        // Step 1: Split at exact overhang boundaries
        std::vector<SplitSegment> overhang_splits = split_at_overhang_boundaries(points);

        // Step 2: For non-overhang segments, apply visibility splitting
        std::vector<SplitSegment> result;
        for (auto &seg : overhang_splits)
        {
            if (seg.is_overhang)
            {
                // Overhang segment - keep as-is, marked to skip
                result.push_back(std::move(seg));
            }
            else if (layer)
            {
                // Non-overhang segment - apply visibility splitting
                auto vis_splits = split_by_visibility(seg.points, config);
                for (auto &vs : vis_splits)
                {
                    result.push_back(std::move(vs));
                }
            }
            else
            {
                // No layer for visibility check - keep as-is, will be fuzzified
                result.push_back(std::move(seg));
            }
        }

        return result;
    };

    // Removed apply_fuzzy_skin_on_polygon - it only checked midpoint.
    // Now we always use segment splitting for proper overhang and visibility handling.

    // Apply segment splitting even when no paint-on regions
    if (perimeter_regions.empty())
    {
        if (should_fuzzify(cfg, static_cast<int>(layer_idx), perimeter_idx, is_contour))
        {
            // Always do overhang detection if lower_slices provided
            // Use combined overhang + visibility splitting
            auto splits = split_polygon_segments(polygon.points, cfg);

            // Cache result for outer perimeter
            // Check for any "all perimeters" mode
            const bool is_all_perimeters_polygon = cfg.type == FuzzySkinType::All ||
                                                   cfg.type == FuzzySkinType::AllWalls;
            if (layer && is_all_perimeters_polygon && perimeter_idx == 0)
            {
                bool had_transitions = splits.size() > 1;
                bool all_skip = !had_transitions && !splits.empty() && splits.front().should_skip;
                set_visibility_state(layer_idx, had_transitions, all_skip);
            }

            Polygon fuzzified_polygon;

            for (auto &split : splits)
            {
                if (!split.should_skip && split.points.size() >= 2)
                {
                    // Non-overhang, non-visible portion - apply fuzzy
                    fuzzy_polyline(split.points, false, slice_z, cfg);
                }
                // Append points (fuzzified or not) to result
                if (!split.points.empty())
                {
                    if (!fuzzified_polygon.empty() && fuzzified_polygon.back() == split.points.front())
                    {
                        fuzzified_polygon.points.pop_back();
                    }
                    Slic3r::append(fuzzified_polygon.points, std::move(split.points));
                }
            }

            if (!fuzzified_polygon.empty() && fuzzified_polygon.front() == fuzzified_polygon.back())
            {
                fuzzified_polygon.points.pop_back();
            }

            // Early return optimization for inner perimeters
            // Check for any "all perimeters" mode
            if (is_all_perimeters_polygon && perimeter_idx > 0)
            {
                const VisibilityState *cached = get_visibility_state(layer_idx);
                if (cached && !cached->had_transitions)
                {
                    if (cached->all_visible)
                    {
                        return polygon; // All skip - return original
                    }
                    else
                    {
                        Polygon full_fuzzy = polygon;
                        fuzzy_polygon(full_fuzzy, slice_z, cfg);
                        return full_fuzzy;
                    }
                }
            }

            return fuzzified_polygon;
        }
        return polygon;
    }

    // Paint-on regions path - use combined overhang + visibility splitting
    PolylineRegionSegments segments = polygon_segmentation(polygon, base_config, perimeter_regions);
    Polygon fuzzified_polygon;

    for (PolylineRegionSegment &segment : segments)
    {
        const PrintRegionConfig &config = *segment.config;
        FuzzySkinConfig seg_cfg = make_fuzzy_config(config);
        // Only set painted perimeter limit for segments actually in painted regions
        // (not for segments using the base config outside painted regions)
        if (segment.config != &base_config)
            set_painted_perimeter_limit(seg_cfg, config);

        if (should_fuzzify(seg_cfg, static_cast<int>(layer_idx), perimeter_idx, is_contour))
        {
            auto splits = split_polygon_segments(segment.polyline.points, seg_cfg);

            for (auto &split : splits)
            {
                if (!split.should_skip && split.points.size() >= 2)
                {
                    fuzzy_polyline(split.points, false, slice_z, seg_cfg);
                }
                if (!split.points.empty())
                {
                    if (!fuzzified_polygon.empty() && fuzzified_polygon.back() == split.points.front())
                    {
                        fuzzified_polygon.points.pop_back();
                    }
                    Slic3r::append(fuzzified_polygon.points, std::move(split.points));
                }
            }
        }
        else
        {
            if (!segment.polyline.empty())
            {
                if (!fuzzified_polygon.empty() && fuzzified_polygon.back() == segment.polyline.front())
                {
                    fuzzified_polygon.points.pop_back();
                }
                Slic3r::append(fuzzified_polygon.points, std::move(segment.polyline.points));
            }
        }
    }

    if (!fuzzified_polygon.empty() && fuzzified_polygon.front() == fuzzified_polygon.back())
    {
        fuzzified_polygon.points.pop_back();
    }

    return fuzzified_polygon;
}

// Arachne version of apply_fuzzy_skin
// Added Layer* parameter for per-segment visibility checks
// Added lower_slices parameter to exclude overhangs
// Added ext_perimeter_width parameter
Arachne::ExtrusionLine apply_fuzzy_skin(const Arachne::ExtrusionLine &extrusion, const PrintRegionConfig &base_config,
                                        const PerimeterRegions &perimeter_regions, const size_t layer_idx,
                                        const size_t perimeter_idx, const bool is_contour, const Layer *layer,
                                        const Polygons *lower_slices, const coord_t ext_perimeter_width)
{
    // Legacy version - delegate to new version with slice_z = 0
    return apply_fuzzy_skin(extrusion, base_config, perimeter_regions, layer_idx, perimeter_idx, is_contour, 0.0, layer,
                            lower_slices, ext_perimeter_width);
}

// New Arachne apply_fuzzy_skin with slice_z
// Added ext_perimeter_width parameter
Arachne::ExtrusionLine apply_fuzzy_skin(const Arachne::ExtrusionLine &extrusion, const PrintRegionConfig &base_config,
                                        const PerimeterRegions &perimeter_regions, const size_t layer_idx,
                                        const size_t perimeter_idx, const bool is_contour, const double slice_z,
                                        const Layer *layer, const Polygons *lower_slices,
                                        const coord_t ext_perimeter_width)
{
    using namespace Slic3r::Algorithm::LineSegmentation;
    using namespace Slic3r::Arachne;

    FuzzySkinConfig cfg = make_fuzzy_config(base_config);

    // Use unified template for visibility/overhang splitting
    // Scale check diameter based on external perimeter width
    const coord_t check_diameter_coarse = (ext_perimeter_width > 0) ? (ext_perimeter_width * 4) : scaled(1.6);
    const coord_t check_diameter_fine = (ext_perimeter_width > 0) ? (ext_perimeter_width / 2) : scaled(0.2);

    // Helper lambda wrapping the unified template
    auto get_splits = [&](const ExtrusionLine &ext, const FuzzySkinConfig &config)
    {
        return split_extrusion_by_visibility_and_overhang(ext, lower_slices, layer, config, check_diameter_coarse,
                                                          check_diameter_fine);
    };

    // Apply segment splitting with overhang exclusion
    if (perimeter_regions.empty())
    {
        if (should_fuzzify(cfg, static_cast<int>(layer_idx), perimeter_idx, is_contour))
        {
            // Use combined overhang + visibility splitting
            auto splits = get_splits(extrusion, cfg);

            // Cache result for outer perimeter
            // Check for any "all perimeters" mode
            const bool is_all_perimeters_ext = cfg.type == FuzzySkinType::All || cfg.type == FuzzySkinType::AllWalls;
            if (layer && is_all_perimeters_ext && perimeter_idx == 0)
            {
                bool had_transitions = splits.size() > 1;
                bool all_skip = !had_transitions && !splits.empty() && splits.front().should_skip;
                set_visibility_state(layer_idx, had_transitions, all_skip);
            }

            ExtrusionLine fuzzified_extrusion(extrusion.inset_idx, extrusion.is_odd, extrusion.is_closed);

            for (auto &split : splits)
            {
                if (!split.should_skip && split.ext.junctions.size() >= 2)
                {
                    fuzzy_extrusion_line(split.ext, slice_z, cfg);
                }
                if (!split.ext.junctions.empty())
                {
                    if (!fuzzified_extrusion.junctions.empty() &&
                        fuzzified_extrusion.junctions.back().p == split.ext.junctions.front().p)
                    {
                        fuzzified_extrusion.junctions.pop_back();
                    }
                    for (auto &j : split.ext.junctions)
                    {
                        fuzzified_extrusion.junctions.push_back(j);
                    }
                }
            }
            return fuzzified_extrusion;
        }
        return extrusion;
    }

    // Paint-on regions path - use combined overhang + visibility splitting
    ExtrusionRegionSegments segments = extrusion_segmentation(extrusion, base_config, perimeter_regions);
    ExtrusionLine fuzzified_extrusion(extrusion.inset_idx, extrusion.is_odd, extrusion.is_closed);

    for (ExtrusionRegionSegment &segment : segments)
    {
        const PrintRegionConfig &config = *segment.config;
        FuzzySkinConfig seg_cfg = make_fuzzy_config(config);
        // Only set painted perimeter limit for segments actually in painted regions
        if (segment.config != &base_config)
            set_painted_perimeter_limit(seg_cfg, config);

        if (should_fuzzify(seg_cfg, static_cast<int>(layer_idx), perimeter_idx, is_contour))
        {
            auto splits = get_splits(segment.extrusion, seg_cfg);
            for (auto &split : splits)
            {
                if (!split.should_skip && split.ext.size() >= 2)
                    fuzzy_extrusion_line(split.ext, slice_z, seg_cfg);
                if (!split.ext.empty())
                {
                    if (!fuzzified_extrusion.empty() && fuzzified_extrusion.back().p == split.ext.front().p)
                        fuzzified_extrusion.junctions.pop_back();
                    Slic3r::append(fuzzified_extrusion.junctions, std::move(split.ext.junctions));
                }
            }
        }
        else
        {
            if (!segment.extrusion.empty())
            {
                if (!fuzzified_extrusion.empty() && fuzzified_extrusion.back().p == segment.extrusion.front().p)
                    fuzzified_extrusion.junctions.pop_back();
                Slic3r::append(fuzzified_extrusion.junctions, std::move(segment.extrusion.junctions));
            }
        }
    }

    return fuzzified_extrusion;
}

// Athena version of fuzzy_extrusion_line
void fuzzy_extrusion_line(Athena::ExtrusionLine &ext_lines, const double fuzzy_skin_thickness,
                          const double fuzzy_skin_point_distance)
{
    if (ext_lines.size() < 2)
        return;

    const double min_dist_between_points =
        fuzzy_skin_point_distance * 3. /
        4.; // hardcoded: the point distance may vary between 3/4 and 5/4 the supplied value
    const double range_random_point_dist = fuzzy_skin_point_distance / 2.;
    double dist_left_over = random_value() *
                            (min_dist_between_points /
                             2.); // the distance to be traversed on the line before making the first new point

    Athena::ExtrusionJunction *p0 = &ext_lines.front();
    Athena::ExtrusionJunctions out;
    out.reserve(ext_lines.size());
    for (auto &p1 : ext_lines)
    {
        if (p0->p == p1.p)
        {
            // Copy the first point.
            out.emplace_back(p1.p, p1.w, p1.perimeter_index);
            continue;
        }

        // 'a' is the (next) new point between p0 and p1
        Vec2d p0p1 = (p1.p - p0->p).cast<double>();
        double p0p1_size = p0p1.norm();
        double p0pa_dist = dist_left_over;
        for (; p0pa_dist < p0p1_size; p0pa_dist += min_dist_between_points + random_value() * range_random_point_dist)
        {
            double r = random_value() * (fuzzy_skin_thickness * 2.) - fuzzy_skin_thickness;
            out.emplace_back(
                p0->p + (p0p1 * (p0pa_dist / p0p1_size) + perp(p0p1).cast<double>().normalized() * r).cast<coord_t>(),
                p1.w, p1.perimeter_index);
        }

        dist_left_over = p0pa_dist - p0p1_size;
        p0 = &p1;
    }

    while (out.size() < 3)
    {
        size_t point_idx = ext_lines.size() - 2;
        out.emplace_back(ext_lines[point_idx].p, ext_lines[point_idx].w, ext_lines[point_idx].perimeter_index);
        if (point_idx == 0)
        {
            break;
        }

        --point_idx;
    }

    if (ext_lines.back().p == ext_lines.front().p)
    {
        // Connect endpoints.
        out.front().p = out.back().p;
    }

    if (out.size() >= 3)
    {
        ext_lines.junctions = std::move(out);
    }
}

// Shape-following Athena extrusion line
static void fuzzy_extrusion_line_shape_following(Athena::ExtrusionLine &ext_lines, const double slice_z,
                                                 const FuzzySkinConfig &cfg)
{
    if (ext_lines.size() < 2)
        return;

    const bool closed = ext_lines.is_closed;
    const std::vector<Athena::ExtrusionJunction> &points = ext_lines.junctions;

    std::unique_ptr<NoiseModule> noise = createNoiseModule(cfg);
    std::vector<Athena::ExtrusionJunction> out;

    const double line_unit_length = 2. / 3. * cfg.point_distance;
    const double point_min_delta = 2e-1 * line_unit_length;
    const double min_extrusion_width = 0.01;
    const int n_point = static_cast<int>(points.size());
    int n_seg = n_point;

    if (!closed || (closed && (points[0].p == points[n_seg - 1].p)))
        --n_seg;

    double total_length = 0;
    for (int i = 0; i < n_seg; ++i)
    {
        total_length += (points[(i + 1) % n_point].p - points[i].p).cast<double>().norm();
    }

    out.reserve(n_seg + static_cast<size_t>(std::ceil(total_length / line_unit_length)));

    Vec2d seg_dir;
    Vec2d seg_perp = closed
                         ? perp((points[0].p - points[(n_seg - 1 + n_point) % n_point].p).cast<double>().normalized())
                         : perp((points[1].p - points[0].p).cast<double>().normalized());
    Athena::ExtrusionJunction p_ref = points[0];

    double x_prev = 0;
    double x_next = total_length < (2. * line_unit_length)
                        ? total_length
                        : line_unit_length +
                              random_value() * std::min(line_unit_length, total_length - 2 * line_unit_length);

    double x_prev_corner = 0;
    double x_next_corner = 0;
    int corner_idx = 0;

    double y_0 = noise->getValue(unscale<double>(p_ref.p.x()), unscale<double>(p_ref.p.y()), slice_z) * cfg.thickness;
    double y_prev = y_0;
    Point next_sample_pt = p_ref.p;
    double y_next = noise->getValue(unscale<double>(next_sample_pt.x()), unscale<double>(next_sample_pt.y()), slice_z) *
                    cfg.thickness;

    while (x_prev < total_length)
    {
        while (x_next_corner <= x_next)
        {
            if (corner_idx == n_seg)
                break;
            double y = lerp(y_prev, y_next, (x_next_corner - x_prev) / (x_next - x_prev));
            Vec2d prev_perp = seg_perp;

            p_ref = points[corner_idx];
            Vec2d seg = (points[(corner_idx + 1) % n_point].p - p_ref.p).cast<double>();
            double seg_length = seg.norm();
            seg_dir = seg.normalized();
            seg_perp = perp(seg_dir);

            Vec2d corner_perp = seg_perp.dot(prev_perp) > -0.99 ? Vec2d((seg_perp + prev_perp).normalized()) : seg_dir;

            switch (cfg.mode)
            {
            case FuzzySkinMode::Displacement:
                out.emplace_back(p_ref.p + (y * corner_perp).cast<coord_t>(), p_ref.w, p_ref.perimeter_index);
                break;
            case FuzzySkinMode::Extrusion:
                out.emplace_back(p_ref.p, std::max(p_ref.w + y + min_extrusion_width, min_extrusion_width),
                                 p_ref.perimeter_index);
                break;
            case FuzzySkinMode::Combined:
            {
                double rad = std::max(p_ref.w + y + min_extrusion_width, min_extrusion_width);
                out.emplace_back(p_ref.p + (((rad - p_ref.w) / 2) * corner_perp).cast<coord_t>(), rad,
                                 p_ref.perimeter_index);
                break;
            }
            }

            x_prev_corner = x_next_corner;
            x_next_corner += seg_length;
            ++corner_idx;
        }

        if (!((x_next - x_prev_corner) < point_min_delta || (x_next_corner - x_next) < point_min_delta))
        {
            Point new_pos = p_ref.p + ((x_next - x_prev_corner) * seg_dir + y_next * seg_perp).cast<coord_t>();
            switch (cfg.mode)
            {
            case FuzzySkinMode::Displacement:
                out.emplace_back(new_pos, p_ref.w, p_ref.perimeter_index);
                break;
            case FuzzySkinMode::Extrusion:
            {
                Point base_pos = p_ref.p + ((x_next - x_prev_corner) * seg_dir).cast<coord_t>();
                out.emplace_back(base_pos, std::max(p_ref.w + y_next + min_extrusion_width, min_extrusion_width),
                                 p_ref.perimeter_index);
                break;
            }
            case FuzzySkinMode::Combined:
            {
                double rad = std::max(p_ref.w + y_next + min_extrusion_width, min_extrusion_width);
                Point base_pos = p_ref.p + ((x_next - x_prev_corner) * seg_dir).cast<coord_t>();
                out.emplace_back(base_pos + (((rad - p_ref.w) / 2) * seg_perp).cast<coord_t>(), rad,
                                 p_ref.perimeter_index);
                break;
            }
            }
        }

        x_prev = x_next;
        x_next = x_prev > total_length - (2. * line_unit_length)
                     ? total_length
                     : x_prev + line_unit_length +
                           random_value() * std::min(line_unit_length, total_length - x_prev - 2. * line_unit_length);

        y_prev = y_next;
        if (corner_idx < n_seg)
        {
            next_sample_pt = p_ref.p + ((x_next - x_prev_corner) * seg_dir).cast<coord_t>();
        }
        y_next = (closed && x_next == total_length) ? y_0
                                                    : noise->getValue(unscale<double>(next_sample_pt.x()),
                                                                      unscale<double>(next_sample_pt.y()), slice_z) *
                                                          cfg.thickness;
    }

    if (closed)
        out.emplace_back(out[0]);
    else
        out.emplace_back(points[n_seg].p + (y_next * seg_perp).cast<coord_t>(), p_ref.w, p_ref.perimeter_index);

    out.shrink_to_fit();
    ext_lines.junctions = std::move(out);
}

// Athena version with structured noise support
void fuzzy_extrusion_line(Athena::ExtrusionLine &ext_lines, const double slice_z, const FuzzySkinConfig &cfg)
{
    if (ext_lines.size() < 2)
        return;

    // Dispatch based on point placement algorithm
    if (cfg.point_placement == FuzzySkinPointPlacement::ShapeFollowing)
    {
        fuzzy_extrusion_line_shape_following(ext_lines, slice_z, cfg);
        return;
    }

    std::unique_ptr<NoiseModule> noise = createNoiseModule(cfg);

    const double min_dist_between_points = cfg.point_distance * 3. / 4.;
    const double range_random_point_dist = cfg.point_distance / 2.;
    const double min_extrusion_width = 0.01;
    double dist_left_over = random_value() * (min_dist_between_points / 2.);

    Athena::ExtrusionJunction *p0 = &ext_lines.front();
    Athena::ExtrusionJunctions out;
    out.reserve(ext_lines.size());

    for (auto &p1 : ext_lines)
    {
        if (p0->p == p1.p)
        {
            out.emplace_back(p1.p, p1.w, p1.perimeter_index);
            continue;
        }

        Vec2d p0p1 = (p1.p - p0->p).cast<double>();
        double p0p1_size = p0p1.norm();
        double p0pa_dist = dist_left_over;

        for (; p0pa_dist < p0p1_size; p0pa_dist += min_dist_between_points + random_value() * range_random_point_dist)
        {
            Point pa = p0->p + (p0p1 * (p0pa_dist / p0p1_size)).cast<coord_t>();
            double r = noise->getValue(unscale<double>(pa.x()), unscale<double>(pa.y()), slice_z) * cfg.thickness;

            switch (cfg.mode)
            {
            case FuzzySkinMode::Displacement:
                out.emplace_back(pa + (perp(p0p1).cast<double>().normalized() * r).cast<coord_t>(), p1.w,
                                 p1.perimeter_index);
                break;
            case FuzzySkinMode::Extrusion:
                out.emplace_back(pa, std::max(p1.w + r + min_extrusion_width, min_extrusion_width), p1.perimeter_index);
                break;
            case FuzzySkinMode::Combined:
            {
                double rad = std::max(p1.w + r + min_extrusion_width, min_extrusion_width);
                out.emplace_back(pa + (perp(p0p1).cast<double>().normalized() * ((rad - p1.w) / 2)).cast<coord_t>(),
                                 rad, p1.perimeter_index);
                break;
            }
            }
        }

        dist_left_over = p0pa_dist - p0p1_size;
        p0 = &p1;
    }

    while (out.size() < 3)
    {
        size_t point_idx = ext_lines.size() - 2;
        out.emplace_back(ext_lines[point_idx].p, ext_lines[point_idx].w, ext_lines[point_idx].perimeter_index);
        if (point_idx == 0)
        {
            break;
        }
        --point_idx;
    }

    if (ext_lines.back().p == ext_lines.front().p)
    {
        out.front().p = out.back().p;
        out.front().w = out.back().w;
    }

    if (out.size() >= 3)
    {
        ext_lines.junctions = std::move(out);
    }
}

// Added Layer* parameter for per-segment visibility checks
// Added lower_slices parameter to exclude overhangs
// Added ext_perimeter_width parameter
Athena::ExtrusionLine apply_fuzzy_skin(const Athena::ExtrusionLine &extrusion, const PrintRegionConfig &base_config,
                                       const PerimeterRegions &perimeter_regions, const size_t layer_idx,
                                       const size_t perimeter_idx, const bool is_contour, const Layer *layer,
                                       const Polygons *lower_slices, const coord_t ext_perimeter_width)
{
    // Legacy version - delegate to new version with slice_z = 0
    return apply_fuzzy_skin(extrusion, base_config, perimeter_regions, layer_idx, perimeter_idx, is_contour, 0.0, layer,
                            lower_slices, ext_perimeter_width);
}

// New Athena apply_fuzzy_skin with slice_z
// Added ext_perimeter_width parameter
Athena::ExtrusionLine apply_fuzzy_skin(const Athena::ExtrusionLine &extrusion, const PrintRegionConfig &base_config,
                                       const PerimeterRegions &perimeter_regions, const size_t layer_idx,
                                       const size_t perimeter_idx, const bool is_contour, const double slice_z,
                                       const Layer *layer, const Polygons *lower_slices,
                                       const coord_t ext_perimeter_width)
{
    using namespace Slic3r::Algorithm::LineSegmentation;
    using namespace Slic3r::Athena;

    FuzzySkinConfig cfg = make_fuzzy_config(base_config);

    // Use unified template for visibility/overhang splitting
    // Scale check diameter based on external perimeter width
    // Coarse detection uses 4x perimeter width, fine binary search uses 0.5x
    // No point refining beyond the probe size - that's our measurement resolution
    const coord_t check_diameter_coarse = (ext_perimeter_width > 0) ? (ext_perimeter_width * 4) : scaled(1.6);
    const coord_t check_diameter_fine = (ext_perimeter_width > 0) ? (ext_perimeter_width / 2) : scaled(0.2);

    // Helper lambda wrapping the unified template
    auto get_splits = [&](const ExtrusionLine &ext, const FuzzySkinConfig &config)
    {
        return split_extrusion_by_visibility_and_overhang(ext, lower_slices, layer, config, check_diameter_coarse,
                                                          check_diameter_fine);
    };

    // Apply segment splitting with overhang exclusion
    if (perimeter_regions.empty())
    {
        if (should_fuzzify(cfg, static_cast<int>(layer_idx), perimeter_idx, is_contour))
        {
            // Use combined overhang + visibility splitting
            auto splits = get_splits(extrusion, cfg);

            // Cache result for outer perimeter
            // Check for any "all perimeters" mode
            const bool is_all_perimeters_ext = cfg.type == FuzzySkinType::All || cfg.type == FuzzySkinType::AllWalls;
            if (layer && is_all_perimeters_ext && perimeter_idx == 0)
            {
                bool had_transitions = splits.size() > 1;
                bool all_skip = !had_transitions && !splits.empty() && splits.front().should_skip;
                set_visibility_state(layer_idx, had_transitions, all_skip);
            }

            ExtrusionLine fuzzified_extrusion(extrusion.inset_idx, extrusion.is_odd, extrusion.is_closed);

            for (auto &split : splits)
            {
                if (!split.should_skip && split.ext.junctions.size() >= 2)
                {
                    fuzzy_extrusion_line(split.ext, slice_z, cfg);
                }
                if (!split.ext.junctions.empty())
                {
                    if (!fuzzified_extrusion.junctions.empty() &&
                        fuzzified_extrusion.junctions.back().p == split.ext.junctions.front().p)
                    {
                        fuzzified_extrusion.junctions.pop_back();
                    }
                    for (auto &j : split.ext.junctions)
                    {
                        fuzzified_extrusion.junctions.push_back(j);
                    }
                }
            }
            return fuzzified_extrusion;
        }
        return extrusion;
    }

    // Paint-on regions path
    AthenaExtrusionRegionSegments segments = extrusion_segmentation(extrusion, base_config, perimeter_regions);
    ExtrusionLine fuzzified_extrusion(extrusion.inset_idx, extrusion.is_odd, extrusion.is_closed);

    for (AthenaExtrusionRegionSegment &segment : segments)
    {
        const PrintRegionConfig &config = *segment.config;
        FuzzySkinConfig seg_cfg = make_fuzzy_config(config);
        // Only set painted perimeter limit for segments actually in painted regions
        if (segment.config != &base_config)
            set_painted_perimeter_limit(seg_cfg, config);

        // Split at visibility boundaries
        if (layer && should_fuzzify(seg_cfg, static_cast<int>(layer_idx), perimeter_idx, is_contour))
        {
            auto splits = get_splits(segment.extrusion, seg_cfg);
            for (auto &split : splits)
            {
                if (!split.should_skip && split.ext.size() >= 2)
                    fuzzy_extrusion_line(split.ext, slice_z, seg_cfg);
                if (!split.ext.empty())
                {
                    if (!fuzzified_extrusion.empty() && fuzzified_extrusion.back().p == split.ext.front().p)
                        fuzzified_extrusion.junctions.pop_back();
                    Slic3r::append(fuzzified_extrusion.junctions, std::move(split.ext.junctions));
                }
            }
        }
        else
        {
            if (!segment.extrusion.empty())
            {
                if (!fuzzified_extrusion.empty() && fuzzified_extrusion.back().p == segment.extrusion.front().p)
                    fuzzified_extrusion.junctions.pop_back();
                Slic3r::append(fuzzified_extrusion.junctions, std::move(segment.extrusion.junctions));
            }
        }
    }

    assert(!fuzzified_extrusion.empty());
    return fuzzified_extrusion;
}

// Visibility splitting for flow reduction
std::vector<VisibilitySegment> split_polygon_by_visibility(const Polygon &polygon, const Layer *layer,
                                                           const PrintRegionConfig &config, coord_t ext_perimeter_width)
{
    std::vector<VisibilitySegment> result;

    // If no layer or empty polygon, return single non-visible segment
    if (!layer || polygon.points.size() < 2)
    {
        result.push_back({polygon.points, false});
        return result;
    }

    // Get visibility detection interval based on config setting
    double visibility_interval;
    switch (config.top_surface_visibility_detection.value)
    {
    case TopSurfaceVisibilityDetection::tsvdPrecise:
        visibility_interval = 1.0;
        break;
    case TopSurfaceVisibilityDetection::tsvdStandard:
        visibility_interval = 2.0;
        break;
    case TopSurfaceVisibilityDetection::tsvdRelaxed:
        visibility_interval = 4.0;
        break;
    case TopSurfaceVisibilityDetection::tsvdMinimal:
        visibility_interval = 8.0;
        break;
    default:
        visibility_interval = 2.0;
        break;
    }

    // Check diameter for visibility (same as fuzzy skin)
    const coord_t check_diameter_coarse = (ext_perimeter_width > 0) ? (ext_perimeter_width * 4) : scaled(1.6);
    const coord_t check_diameter_fine = (ext_perimeter_width > 0) ? (ext_perimeter_width / 2) : scaled(0.2);

    // Lambda to check if point is visible from top
    auto point_is_visible = [layer, check_diameter_coarse](const Point &pt) -> bool
    {
        return layer->is_visible_from_top_or_bottom(pt, check_diameter_coarse, true, false);
    };

    // Binary search to find exact visibility boundary
    auto find_visibility_boundary = [layer, check_diameter_coarse, check_diameter_fine](const Point &p1,
                                                                                        const Point &p2) -> Point
    {
        Point visible_pt = p1;
        Point hidden_pt = p2;
        bool p1_visible = layer->is_visible_from_top_or_bottom(p1, check_diameter_coarse, true, false);
        if (p1_visible)
            std::swap(visible_pt, hidden_pt);

        const double min_precision = unscale<double>(check_diameter_fine);
        double distance = unscale<double>((hidden_pt - visible_pt).cast<double>().norm());

        while (distance > min_precision)
        {
            Point mid((visible_pt.x() + hidden_pt.x()) / 2, (visible_pt.y() + hidden_pt.y()) / 2);
            bool mid_visible = layer->is_visible_from_top_or_bottom(mid, check_diameter_fine, true, false);
            if (mid_visible)
            {
                hidden_pt = mid;
            }
            else
            {
                visible_pt = mid;
            }
            distance = unscale<double>((hidden_pt - visible_pt).cast<double>().norm());
        }
        return Point((visible_pt.x() + hidden_pt.x()) / 2, (visible_pt.y() + hidden_pt.y()) / 2);
    };

    // Split polygon at visibility boundaries using interval sampling
    const double sample_interval = visibility_interval;
    bool current_visible = point_is_visible(polygon.points[0]);
    Points current_segment;
    current_segment.push_back(polygon.points[0]);
    Point last_known_state_pt = polygon.points[0];

    for (size_t i = 1; i < polygon.points.size(); ++i)
    {
        const Point &prev_pt = polygon.points[i - 1];
        const Point &curr_pt = polygon.points[i];
        double seg_len = unscale<double>((curr_pt - prev_pt).cast<double>().norm());

        if (seg_len <= sample_interval)
        {
            bool end_visible = point_is_visible(curr_pt);
            if (end_visible != current_visible)
            {
                Point boundary = find_visibility_boundary(last_known_state_pt, curr_pt);
                current_segment.push_back(boundary);
                result.push_back({std::move(current_segment), current_visible});
                current_segment.clear();
                current_segment.push_back(boundary);
                current_visible = end_visible;
            }
            current_segment.push_back(curr_pt);
            last_known_state_pt = curr_pt;
        }
        else
        {
            Vec2d direction = (curr_pt - prev_pt).cast<double>();
            double dir_len = direction.norm();
            Vec2d dir_unit = direction / dir_len;

            double distance_along = sample_interval;
            while (distance_along < seg_len)
            {
                Point sample_pt(prev_pt.x() + coord_t(dir_unit.x() * scaled(distance_along)),
                                prev_pt.y() + coord_t(dir_unit.y() * scaled(distance_along)));
                bool sample_visible = point_is_visible(sample_pt);

                if (sample_visible != current_visible)
                {
                    Point boundary = find_visibility_boundary(last_known_state_pt, sample_pt);
                    current_segment.push_back(boundary);
                    result.push_back({std::move(current_segment), current_visible});
                    current_segment.clear();
                    current_segment.push_back(boundary);
                    current_visible = sample_visible;
                }
                last_known_state_pt = sample_pt;
                distance_along += sample_interval;
            }
            bool end_visible = point_is_visible(curr_pt);
            if (end_visible != current_visible)
            {
                Point boundary = find_visibility_boundary(last_known_state_pt, curr_pt);
                current_segment.push_back(boundary);
                result.push_back({std::move(current_segment), current_visible});
                current_segment.clear();
                current_segment.push_back(boundary);
                current_visible = end_visible;
            }
            current_segment.push_back(curr_pt);
            last_known_state_pt = curr_pt;
        }
    }

    // Handle the closing edge (from last point back to first)
    const Point &last_pt = polygon.points.back();
    const Point &first_pt = polygon.points.front();
    double seg_len = unscale<double>((first_pt - last_pt).cast<double>().norm());

    if (seg_len > sample_interval)
    {
        Vec2d direction = (first_pt - last_pt).cast<double>();
        double dir_len = direction.norm();
        Vec2d dir_unit = direction / dir_len;

        double distance_along = sample_interval;
        while (distance_along < seg_len)
        {
            Point sample_pt(last_pt.x() + coord_t(dir_unit.x() * scaled(distance_along)),
                            last_pt.y() + coord_t(dir_unit.y() * scaled(distance_along)));
            bool sample_visible = point_is_visible(sample_pt);

            if (sample_visible != current_visible)
            {
                Point boundary = find_visibility_boundary(last_known_state_pt, sample_pt);
                current_segment.push_back(boundary);
                result.push_back({std::move(current_segment), current_visible});
                current_segment.clear();
                current_segment.push_back(boundary);
                current_visible = sample_visible;
            }
            last_known_state_pt = sample_pt;
            distance_along += sample_interval;
        }
    }

    // Add the final segment
    if (!current_segment.empty())
    {
        result.push_back({std::move(current_segment), current_visible});
    }

    return result;
}

} // namespace Slic3r::Feature::FuzzySkin
