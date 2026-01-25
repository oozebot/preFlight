///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2023 - 2024 Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "Wipe.hpp"

#include <string_view>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <cinttypes>

#include "../GCode.hpp"
#include "libslic3r/Extruder.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/GCode/GCodeWriter.hpp"
#include "libslic3r/GCode/SmoothPath.hpp"
#include "libslic3r/Geometry/ArcWelder.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/libslic3r.h"

using namespace std::string_view_literals;

namespace Slic3r::GCode
{

void Wipe::init(const PrintConfig &config, const std::vector<unsigned int> &extruders)
{
    this->reset_path();

    // Calculate maximum wipe length to accumulate by the wipe cache.
    // Paths longer than wipe_xy should never be needed for the wipe move.
    double wipe_xy = 0;
    const bool multimaterial = extruders.size() > 1;
    for (auto id : extruders)
    {
        double wipe_length = config.wipe_length.get_at(id);
        if (wipe_length > 0)
        {
            // User specified explicit wipe length - works independently of "wipe while retracting"
            wipe_xy = std::max(wipe_xy, wipe_length);
        }
        else if (config.wipe.get_at(id))
        {
            // "Wipe while retracting" enabled without explicit length - calculate from retract length
            const double xy_to_e = this->calc_xy_to_e_ratio(config, id);
            wipe_xy = std::max(wipe_xy, config.retract_length.get_at(id) / xy_to_e);
            if (multimaterial)
                wipe_xy = std::max(wipe_xy, config.retract_length_toolchange.get_at(id) / xy_to_e);
        }
    }

    if (wipe_xy == 0)
        this->disable();
    else
        this->enable(wipe_xy);
}

void Wipe::set_path(SmoothPath &&path)
{
    this->reset_path();

    if (this->enabled() && !path.empty())
    {
        const coord_t wipe_len_max_scaled = scaled(m_wipe_len_max);
        m_path = std::move(path.front().path);
        int64_t len = Geometry::ArcWelder::estimate_path_length(m_path);
        for (auto it = std::next(path.begin()); len < wipe_len_max_scaled && it != path.end(); ++it)
        {
            if (it->path_attributes.role.is_bridge())
                break; // Do not perform a wipe on bridges.
            assert(it->path.size() >= 2);
            assert(m_path.back().point == it->path.front().point);
            if (m_path.back().point != it->path.front().point)
                // ExtrusionMultiPath is interrupted in some place. This should not really happen.
                break;
            len += Geometry::ArcWelder::estimate_path_length(it->path);
            m_path.insert(m_path.end(), it->path.begin() + 1, it->path.end());
        }
    }

    assert(m_path.empty() || m_path.size() > 1);
}

std::string Wipe::wipe(GCodeGenerator &gcodegen, bool toolchange)
{
    std::string gcode;
    const Extruder &extruder = *gcodegen.writer().extruder();
    static constexpr const std::string_view wipe_retract_comment = "wipe and retract"sv;
    static constexpr const std::string_view wipe_travel_comment = "wipe"sv;

    const bool use_firmware_retraction = gcodegen.config().use_firmware_retraction.value;
    const bool wipe_while_retracting = gcodegen.config().wipe.get_at(extruder.id());
    const bool wipe_extend = gcodegen.config().wipe_extend.get_at(extruder.id());
    const double wipe_length_setting = gcodegen.config().wipe_length.get_at(extruder.id());

    // Remaining quantized retraction length.
    // retract_length is 0 when:
    // - firmware retraction is on (firmware handles retraction), OR
    // - "wipe while retracting" is off (pure wipe = travel only, no retraction)
    double retract_length = (use_firmware_retraction || !wipe_while_retracting)
                                ? 0
                                : extruder.retract_to_go(toolchange ? extruder.retract_length_toolchange()
                                                                    : extruder.retract_length());

    // Proceed with wipe if:
    // - We have retraction to do (normal wipe while retracting), OR
    // - wipe_length > 0 (explicit pure wipe move), OR
    // - firmware retraction is on (wipe as pure travel, firmware does retraction)
    bool should_wipe = (retract_length > 0 || wipe_length_setting > 0 || use_firmware_retraction) && this->has_path();

    if (should_wipe)
    {
        const double wipe_speed = this->calc_wipe_speed(gcodegen.writer().config);
        const double xy_to_e = this->calc_xy_to_e_ratio(gcodegen.writer().config, extruder.id());

        if (wipe_extend && wipe_length_setting > 0 && m_path.size() >= 2)
        {
            // Calculate direction from the last segment of the path
            Vec2d last_point = gcodegen.point_to_gcode(m_path.back().point + m_offset);
            Vec2d prev_point = gcodegen.point_to_gcode(m_path[m_path.size() - 2].point + m_offset);
            Vec2d direction = (last_point - prev_point);
            double path_len = direction.norm();

            if (path_len > EPSILON)
            {
                direction /= path_len; // Normalize

                // Calculate wipe endpoint
                Vec2d current_pos = gcodegen.point_to_gcode_quantized(*gcodegen.last_position);
                Vec2d wipe_end = GCodeFormatter::quantize(Vec2d(current_pos + direction * wipe_length_setting));

                // Emit wipe
                gcode += ";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Wipe_Start) + "\n";
                gcode += gcodegen.writer().set_speed(wipe_speed * 60, {},
                                                     gcodegen.enable_cooling_markers() ? ";_WIPE"sv : ""sv);

                if (use_firmware_retraction)
                {
                    // Firmware retraction: wipe is pure travel
                    gcode += gcodegen.writer().travel_to_xy(wipe_end, wipe_travel_comment);
                }
                else
                {
                    // Normal retraction: retract during wipe
                    double dE = std::min(retract_length, GCodeFormatter::quantize_e(xy_to_e * wipe_length_setting));
                    if (dE > 0)
                        gcode += gcodegen.writer().extrude_to_xy(wipe_end, -dE, wipe_retract_comment);
                    else
                        gcode += gcodegen.writer().travel_to_xy(wipe_end, wipe_travel_comment);
                }

                gcode += ";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Wipe_End) + "\n";
                gcodegen.last_position = gcodegen.gcode_to_point(wipe_end);
            }

            // Prevent wiping again on the same path.
            this->reset_path();
            return gcode;
        }

        // Normal wipe: follow path (retrace backwards along extrusion path)
        // Delayed emitting of a wipe start tag.
        bool wiped = false;
        auto start_wipe = [&wiped, &gcode, &gcodegen, wipe_speed]()
        {
            if (!wiped)
            {
                wiped = true;
                gcode += ";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Wipe_Start) + "\n";
                gcode += gcodegen.writer().set_speed(wipe_speed * 60, {},
                                                     gcodegen.enable_cooling_markers() ? ";_WIPE"sv : ""sv);
            }
        };

        double wipe_distance_remaining = (wipe_length_setting > 0) ? wipe_length_setting
                                                                   : std::numeric_limits<double>::max();

        auto wipe_linear = [&gcode, &gcodegen, &retract_length, &wipe_distance_remaining, xy_to_e,
                            use_firmware_retraction, wipe_length_setting](const Vec2d &prev_quantized, Vec2d &p)
        {
            Vec2d p_quantized = GCodeFormatter::quantize(p);
            if (p_quantized == prev_quantized)
            {
                p = p_quantized;
                return false;
            }
            double segment_length = (p_quantized - prev_quantized).norm();

            // Check wipe_length limit
            bool done = false;
            if (segment_length > wipe_distance_remaining)
            {
                // Shorten to remaining wipe distance
                p = GCodeFormatter::quantize(
                    Vec2d(prev_quantized + (p - prev_quantized) * (wipe_distance_remaining / segment_length)));
                segment_length = wipe_distance_remaining;
                done = true;
            }
            else
            {
                p = p_quantized;
            }
            wipe_distance_remaining -= segment_length;

            // Pure wipe (no retraction) or firmware retraction: just travel
            if (use_firmware_retraction || retract_length <= EPSILON)
            {
                gcode += gcodegen.writer().travel_to_xy(p, wipe_travel_comment);
            }
            else
            {
                // Normal retraction: retract during wipe
                // Quantize E axis as it is to be extruded as a whole segment.
                double dE = GCodeFormatter::quantize_e(xy_to_e * segment_length);
                if (dE > retract_length - EPSILON)
                {
                    if (dE > retract_length + EPSILON)
                        // Shorten the segment.
                        p = GCodeFormatter::quantize(
                            Vec2d(prev_quantized + (p - prev_quantized) * (retract_length / dE)));
                    else
                        p = p_quantized;
                    dE = retract_length;
                    // Only mark done if we've also hit wipe_length limit or no limit set
                    if (wipe_distance_remaining <= EPSILON)
                        done = true;
                }
                if (dE > 0)
                    gcode += gcodegen.writer().extrude_to_xy(p, -dE, wipe_retract_comment);
                else
                    gcode += gcodegen.writer().travel_to_xy(p, wipe_travel_comment);
                retract_length -= dE;
            }

            // Done if we've exhausted wipe distance OR (for non-firmware with no wipe_length) retract length
            if (wipe_distance_remaining <= EPSILON)
                done = true;
            if (!use_firmware_retraction && retract_length <= EPSILON && wipe_length_setting <= 0)
                done = true;

            return done;
        };

        auto wipe_arc = [&gcode, &gcodegen, &retract_length, &wipe_distance_remaining, xy_to_e, &wipe_linear,
                         use_firmware_retraction,
                         wipe_length_setting](const Vec2d &prev_quantized, Vec2d &p, double radius_in, const bool ccw)
        {
            Vec2d p_quantized = GCodeFormatter::quantize(p);
            if (p_quantized == prev_quantized)
            {
                p = p_quantized;
                return false;
            }
            // Use the exact radius for calculating the IJ values, no quantization.
            double radius = radius_in;
            if (radius == 0)
                // Degenerated arc after quantization. Process it as if it was a line segment.
                return wipe_linear(prev_quantized, p);
            Vec2d center = Geometry::ArcWelder::arc_center(prev_quantized.cast<double>(), p_quantized.cast<double>(),
                                                           double(radius), ccw);
            float angle = Geometry::ArcWelder::arc_angle(prev_quantized.cast<double>(), p_quantized.cast<double>(),
                                                         double(radius));
            assert(angle > 0);
            double segment_length = angle * std::abs(radius);

            // Check wipe_length limit
            bool done = false;
            if (segment_length > wipe_distance_remaining)
            {
                // Shorten the arc to remaining wipe distance
                center = Geometry::ArcWelder::arc_center(prev_quantized.cast<double>(), p.cast<double>(),
                                                         double(radius), ccw);
                angle = Geometry::ArcWelder::arc_angle(prev_quantized.cast<double>(), p.cast<double>(), double(radius));
                segment_length = angle * std::abs(radius);
                double ratio = wipe_distance_remaining / segment_length;
                p = GCodeFormatter::quantize(
                    Vec2d(center + Eigen::Rotation2D((ccw ? angle : -angle) * ratio) * (prev_quantized - center)));
                segment_length = wipe_distance_remaining;
                done = true;
            }
            else
            {
                p = p_quantized;
            }
            wipe_distance_remaining -= segment_length;

            // Pure wipe (no retraction) or firmware retraction: just travel
            if (use_firmware_retraction || retract_length <= EPSILON)
            {
                Vec2d ij = GCodeFormatter::quantize(Vec2d(center - prev_quantized));
                if (ij == Vec2d::Zero())
                    gcode += gcodegen.writer().travel_to_xy(p, wipe_travel_comment);
                else
                    gcode += gcodegen.writer().travel_to_xy_G2G3IJ(p, ij, ccw, wipe_travel_comment);
            }
            else
            {
                double dE = GCodeFormatter::quantize_e(xy_to_e * segment_length);
                if (dE > retract_length - EPSILON)
                {
                    if (dE > retract_length + EPSILON)
                    {
                        // Shorten the segment. Recalculate the arc from the unquantized end coordinate.
                        center = Geometry::ArcWelder::arc_center(prev_quantized.cast<double>(), p.cast<double>(),
                                                                 double(radius), ccw);
                        angle = Geometry::ArcWelder::arc_angle(prev_quantized.cast<double>(), p.cast<double>(),
                                                               double(radius));
                        segment_length = angle * std::abs(radius);
                        dE = xy_to_e * segment_length;
                        p = GCodeFormatter::quantize(
                            Vec2d(center + Eigen::Rotation2D((ccw ? angle : -angle) * (retract_length / dE)) *
                                               (prev_quantized - center)));
                    }
                    else
                        p = p_quantized;
                    dE = retract_length;
                    if (wipe_distance_remaining <= EPSILON)
                        done = true;
                }
                assert(dE >= 0);
                if (dE > 0)
                {
                    // Calculate quantized IJ circle center offset.
                    Vec2d ij = GCodeFormatter::quantize(Vec2d(center - prev_quantized));
                    if (ij == Vec2d::Zero())
                        // Degenerated arc after quantization. Process it as if it was a line segment.
                        return wipe_linear(prev_quantized, p);
                    // The arc is valid.
                    gcode += gcodegen.writer().extrude_to_xy_G2G3IJ(p, ij, ccw, -dE, wipe_retract_comment);
                }
                else
                {
                    Vec2d ij = GCodeFormatter::quantize(Vec2d(center - prev_quantized));
                    if (ij == Vec2d::Zero())
                        gcode += gcodegen.writer().travel_to_xy(p, wipe_travel_comment);
                    else
                        gcode += gcodegen.writer().travel_to_xy_G2G3IJ(p, ij, ccw, wipe_travel_comment);
                }
                retract_length -= dE;
            }

            if (wipe_distance_remaining <= EPSILON)
                done = true;
            if (!use_firmware_retraction && retract_length <= EPSILON && wipe_length_setting <= 0)
                done = true;

            return done;
        };

        // Start with the current position, which may be different from the wipe path start in case of loop clipping.
        Vec2d prev = gcodegen.point_to_gcode_quantized(*gcodegen.last_position);
        auto it = this->path().begin();
        Vec2d p = gcodegen.point_to_gcode(it->point + m_offset);
        ++it;
        bool done = false;
        if (p != prev)
        {
            start_wipe();
            done = wipe_linear(prev, p);
        }
        if (!done)
        {
            prev = p;
            auto end = this->path().end();
            for (; it != end && !done; ++it)
            {
                p = gcodegen.point_to_gcode(it->point + m_offset);
                if (p != prev)
                {
                    start_wipe();
                    if (it->linear() ? wipe_linear(prev, p)
                                     : wipe_arc(prev, p, unscaled<double>(it->radius), it->ccw()))
                        break;
                    prev = p;
                }
            }
        }
        if (wiped)
        {
            // add tag for processor
            assert(p == GCodeFormatter::quantize(p));
            gcode += ";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Wipe_End) + "\n";
            gcodegen.last_position = gcodegen.gcode_to_point(p);
        }
    }

    // Prevent wiping again on the same path.
    this->reset_path();
    return gcode;
}

// Make a little move inwards before leaving loop after path was extruded,
// thus the current extruder position is at the end of a path and the path
// may not be closed in case the loop was clipped to hide a seam.
std::optional<Point> wipe_hide_seam(const SmoothPath &path, const bool path_reversed, const double wipe_length)
{
    assert(!path.empty());
    assert(path.front().path.size() >= 2);
    assert(path.back().path.size() >= 2);

    // Heuristics for estimating whether there is a chance that the wipe move will fit inside a small perimeter
    // or that the wipe move direction could be calculated with reasonable accuracy.
    if (longer_than(path, 2.5 * wipe_length))
    {
        // The print head will be moved away from path end inside the island.
        Point p_current = path.back().path.back().point;
        Point p_next = path.front().path.front().point;
        Point p_prev;
        {
            // Is the seam hiding gap large enough already?
            double l = wipe_length - (p_next - p_current).cast<double>().norm();
            if (l > 0)
            {
                // Not yet.
                std::optional<Point> n = sample_path_point_at_distance_from_start(path, l);
                assert(n);
                if (!n)
                    // Wipe move cannot be calculated, the loop is not long enough. This should not happen due to the longer_than() test above.
                    return {};
            }
            if (std::optional<Point> p = sample_path_point_at_distance_from_start(path, wipe_length); p)
                p_prev = *p;
            else
                // Wipe move cannot be calculated, the loop is not long enough. This should not happen due to the longer_than() test above.
                return {};
        }
        // Detect angle between last and first segment.
        // The side depends on the original winding order of the polygon (left for contours, right for holes).
        double angle_inside = angle(p_next - p_current, p_prev - p_current);
        assert(angle_inside >= -M_PI && angle_inside <= M_PI);
        // 3rd of this angle will be taken, thus make the angle monotonic before interpolation.
        if (path_reversed)
        {
            if (angle_inside > 0)
                angle_inside -= 2.0 * M_PI;
        }
        else
        {
            if (angle_inside < 0)
                angle_inside += 2.0 * M_PI;
        }
        // Rotate the forward segment inside by 1/3 of the wedge angle.
        auto v_rotated = Eigen::Rotation2D(angle_inside) * (p_next - p_current).cast<double>().normalized();
        return p_current + (v_rotated * wipe_length).cast<coord_t>();
    }

    return {};
}

} // namespace Slic3r::GCode
