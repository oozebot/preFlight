///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include <random>

#include "luminary/core/Prelude.hpp"
#include "luminary/walls/segmentation/LineSegmentation.hpp"
#include "luminary/walls/arachne/paths/ExtrusionJunction.hpp"
#include "luminary/walls/arachne/paths/ExtrusionLine.hpp"
#include "luminary/walls/athena/paths/ExtrusionJunction.hpp"
#include "luminary/walls/athena/paths/ExtrusionLine.hpp"
#include "luminary/walls/perimeter/PerimeterGenerator.hpp"
#include "luminary/geometry/primitives/Point.hpp"
#include "luminary/layer/model/Layer.hpp"
#include "luminary/geometry/contours/Polygon.hpp"
#include "luminary/config/catalog/PrintConfig.hpp"
#include "luminary/geometry/clipper/ClipperUtils.hpp"
#include "luminary/core/diagnostics/DebugCounters.hpp"

#include "FuzzySkin.hpp"
#include "ExtrusionLineSplits.hpp"

#include <thread>
#include <unordered_map>
#include <mutex>

using namespace Luminary;

namespace Luminary::Feature::FuzzySkin
{

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

// Extrusion-mode width of a fuzzy junction: the line width plus the noise offset, held above a
// 0.01 mm floor (scaled units, like the widths it applies to) when the noise takes more than the
// line has.
static constexpr double min_extrusion_width = scaled<double>(0.01);
static double fuzzy_width(const double width, const double delta)
{
    const double w = width + delta + min_extrusion_width;
    if (w < min_extrusion_width)
    {
        DBG_COUNT("FUZZY_WIDTH_FLOORED");
        return min_extrusion_width;
    }
    return w;
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
            if (seg_length <= 0.)
            {
                // A duplicate junction has no direction; keep the previous segment's frame.
                ++corner_idx;
                continue;
            }
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
    {
        // A degenerate input can leave the loop without a point to close on.
        if (!out.empty())
            out.emplace_back(out[0]);
    }
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
            if (seg_length <= 0.)
            {
                // A duplicate junction has no direction; keep the previous segment's frame.
                ++corner_idx;
                continue;
            }
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
    {
        // A degenerate input can leave the loop without a point to close on.
        if (!out.empty())
            out.emplace_back(out[0]);
    }
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
            if (seg_length <= 0.)
            {
                // A duplicate junction has no direction; keep the previous segment's frame.
                ++corner_idx;
                continue;
            }
            seg_dir = seg.normalized();
            seg_perp = perp(seg_dir);

            Vec2d corner_perp = seg_perp.dot(prev_perp) > -0.99 ? Vec2d((seg_perp + prev_perp).normalized()) : seg_dir;

            switch (cfg.mode)
            {
            case FuzzySkinMode::Displacement:
                out.emplace_back(p_ref.p + (y * corner_perp).cast<coord_t>(), p_ref.w, p_ref.perimeter_index);
                break;
            case FuzzySkinMode::Extrusion:
                out.emplace_back(p_ref.p, fuzzy_width(p_ref.w, y), p_ref.perimeter_index);
                break;
            case FuzzySkinMode::Combined:
            {
                double rad = fuzzy_width(p_ref.w, y);
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
                out.emplace_back(base_pos, fuzzy_width(p_ref.w, y_next), p_ref.perimeter_index);
                break;
            }
            case FuzzySkinMode::Combined:
            {
                double rad = fuzzy_width(p_ref.w, y_next);
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
    {
        // A degenerate input can leave the loop without a point to close on.
        if (!out.empty())
            out.emplace_back(out[0]);
    }
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
                out.emplace_back(pa, fuzzy_width(p1.w, r), p1.perimeter_index);
                break;
            case FuzzySkinMode::Combined:
            {
                double rad = fuzzy_width(p1.w, r);
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
    using namespace Luminary::Algorithm::LineSegmentation;
    using namespace Luminary::Arachne;

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

        // Same gate as the Athena branch: the splits need the layer.
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
                    Luminary::append(fuzzified_extrusion.junctions, std::move(split.ext.junctions));
                }
            }
        }
        else
        {
            if (!segment.extrusion.empty())
            {
                if (!fuzzified_extrusion.empty() && fuzzified_extrusion.back().p == segment.extrusion.front().p)
                    fuzzified_extrusion.junctions.pop_back();
                Luminary::append(fuzzified_extrusion.junctions, std::move(segment.extrusion.junctions));
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
            if (seg_length <= 0.)
            {
                // A duplicate junction has no direction; keep the previous segment's frame.
                ++corner_idx;
                continue;
            }
            seg_dir = seg.normalized();
            seg_perp = perp(seg_dir);

            Vec2d corner_perp = seg_perp.dot(prev_perp) > -0.99 ? Vec2d((seg_perp + prev_perp).normalized()) : seg_dir;

            switch (cfg.mode)
            {
            case FuzzySkinMode::Displacement:
                out.emplace_back(p_ref.p + (y * corner_perp).cast<coord_t>(), p_ref.w, p_ref.perimeter_index);
                break;
            case FuzzySkinMode::Extrusion:
                out.emplace_back(p_ref.p, fuzzy_width(p_ref.w, y), p_ref.perimeter_index);
                break;
            case FuzzySkinMode::Combined:
            {
                double rad = fuzzy_width(p_ref.w, y);
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
                out.emplace_back(base_pos, fuzzy_width(p_ref.w, y_next), p_ref.perimeter_index);
                break;
            }
            case FuzzySkinMode::Combined:
            {
                double rad = fuzzy_width(p_ref.w, y_next);
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
    {
        // A degenerate input can leave the loop without a point to close on.
        if (!out.empty())
            out.emplace_back(out[0]);
    }
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
                out.emplace_back(pa, fuzzy_width(p1.w, r), p1.perimeter_index);
                break;
            case FuzzySkinMode::Combined:
            {
                double rad = fuzzy_width(p1.w, r);
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
    using namespace Luminary::Algorithm::LineSegmentation;
    using namespace Luminary::Athena;

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
                    Luminary::append(fuzzified_extrusion.junctions, std::move(split.ext.junctions));
                }
            }
        }
        else
        {
            if (!segment.extrusion.empty())
            {
                if (!fuzzified_extrusion.empty() && fuzzified_extrusion.back().p == segment.extrusion.front().p)
                    fuzzified_extrusion.junctions.pop_back();
                Luminary::append(fuzzified_extrusion.junctions, std::move(segment.extrusion.junctions));
            }
        }
    }

    assert(!fuzzified_extrusion.empty());
    return fuzzified_extrusion;
}

} // namespace Luminary::Feature::FuzzySkin
