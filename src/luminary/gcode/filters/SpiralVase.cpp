///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2017 - 2021 Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena
///|/
///|/ Copyright (c) Prusa Research 2017 Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2013 - 2014 Alessandro Ranellucci @alranel
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "SpiralVase.hpp"

#include <utility>
#include <cstddef>
#include <string_view>

#include "luminary/core/diagnostics/DebugCounters.hpp"
#include "luminary/gcode/interpret/GCodeProcessor.hpp"
#include "luminary/geometry/index/AABBTreeLines.hpp"
#include "luminary/gcode/writer/GCodeWriter.hpp"
#include "luminary/geometry/primitives/Line.hpp"
#include "luminary/core/Prelude.hpp"

namespace Luminary
{

static AABBTreeLines::LinesDistancer<Linef> get_layer_distancer(const std::vector<Vec2f> &layer_points)
{
    Linesf lines;
    for (size_t idx = 1; idx < layer_points.size(); ++idx)
        lines.emplace_back(layer_points[idx - 1].cast<double>(), layer_points[idx].cast<double>());

    return AABBTreeLines::LinesDistancer{std::move(lines)};
}

// Lines other than moves the ramp-down pass carries: the acceleration and jerk commands, in their order,
// so the pass runs at the layer's own acceleration and not at the travel acceleration the layer ends on;
// the cooling buffer's markers, which open and close its speed and fan blocks; and the role and width
// tags the G-code viewer reads.
static bool ramp_pass_keeps_line(const GCodeReader::GCodeLine &line)
{
    const std::string_view cmd = line.cmd();
    if (!cmd.empty())
        return cmd == "M204" || cmd == "M205" || cmd == "SET_VELOCITY_LIMIT";
    const std::string_view comment = line.comment();
    return comment.starts_with('_') || comment.starts_with(GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Role)) ||
           comment.starts_with(GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Width));
}

std::string SpiralVase::end_spiral(const std::string &gcode)
{
    m_reader.parse_buffer(gcode);
    m_spiral_live = false;
    if (m_pending_ramp.empty())
        return gcode;
    // The pass flattens the top of the last spiral layer, so it runs before this layer's layer change.
    DBG_COUNT("SPIRAL_EXIT_RAMP");
    std::string out = std::move(m_pending_ramp);
    m_pending_ramp.clear();
    out += gcode;
    return out;
}

std::string SpiralVase::process_layer(const std::string &gcode, bool last_layer)
{
    /*  This post-processor relies on several assumptions:
        - all layers are processed through it, including those that are not supposed
          to be transformed, in order to update the reader with the XY positions
        - each call to this method includes a full layer, with a single Z move
          at the beginning
        - each layer is composed by suitable geometry (i.e. a single complete loop)
        - loops were not clipped before calling this method
        A spiral layer rises by one layer height along its path, so its top is a slope. With relative
        extruder distances every spiral layer that extrudes also builds a ramp-down pass: its extrusion
        moves on their original XY, at the top Z, with the extrusion the slope left out, which makes the
        top flat. The pass is appended on the last layer. Otherwise it is held: the next spiral layer that
        extrudes discards it and continues the spiral, and a layer that does not spiral or prints nothing
        gets it ahead of its own G-code.  */

    // A layer that does not spiral ends the spiral; its G-code only updates the reader.
    if (!m_enabled)
        return end_spiral(gcode);

    // Get total XY length for this layer by summing all extrusion moves.
    float total_layer_length = 0.f;
    float layer_height = 0.f;
    float z = 0.f;

    {
        // Measure the layer on a clone so the real reader's position is not advanced.
        GCodeReader r = m_reader; // clone
        bool set_z = false;
        r.parse_buffer(gcode,
                       [&total_layer_length, &layer_height, &z, &set_z](GCodeReader &reader,
                                                                        const GCodeReader::GCodeLine &line)
                       {
                           if (line.cmd_is("G1"))
                           {
                               if (line.extruding(reader))
                               {
                                   total_layer_length += line.dist_XY(reader);
                               }
                               else if (line.has(Z))
                               {
                                   layer_height += line.dist_Z(reader);
                                   if (!set_z)
                                   {
                                       z = line.new_Z(reader);
                                       set_z = true;
                                   }
                               }
                           }
                       });
    }

    // Remove layer height from initial Z.
    z -= layer_height;

    // Transition tapering and smoothing need relative extruder distances. With absolute distances
    // they are off, because tapering would have to rewrite every E value after the first
    // transition layer. The Z ramp below is applied either way.
    const bool relative_e = m_config.use_relative_e_distances.value;
    if (!relative_e)
        DBG_COUNT("SPIRAL_ABS_E_NO_TRANSITION");

    // A spiral layer without extrusion (an empty top layer) ends the spiral like a flat layer does.
    if (total_layer_length <= 0.f)
        return end_spiral(gcode);

    // This layer continues the spiral, so the previous layer's top is covered and its pass is not needed.
    m_pending_ramp.clear();

    const bool transition_in = !m_spiral_live && relative_e;
    const bool transition_out = relative_e;
    const bool smooth_spiral = m_smooth_spiral && relative_e;

    const AABBTreeLines::LinesDistancer previous_layer_distancer = get_layer_distancer(m_previous_layer);
    Vec2f last_point = m_previous_layer.empty() ? Vec2f::Zero() : m_previous_layer.back();
    float len = 0.f;

    std::string new_gcode, ramp_gcode;
    std::vector<Vec2f> current_layer;
    m_reader.parse_buffer(
        gcode,
        [z, total_layer_length, layer_height, transition_in, transition_out, smooth_spiral,
         max_xy_smoothing = m_max_xy_smoothing, &len, &last_point, &new_gcode, &ramp_gcode, &current_layer,
         &previous_layer_distancer](GCodeReader &reader, GCodeReader::GCodeLine line)
        {
            if (line.cmd_is("G1"))
            {
                if (line.has_z())
                {
                    // If this is the initial Z move of the layer, replace it with a
                    // (redundant) move to the last Z of previous layer.
                    line.set(reader, Z, z);
                    new_gcode += line.raw() + '\n';
                    return;
                }
                else if (line.has_x() || line.has_y())
                { // Sometimes lines have X/Y but the move is to the last position.
                    if (const float dist_XY = line.dist_XY(reader); dist_XY > 0 && line.extruding(reader))
                    { // Exclude wipe and retract
                        len += dist_XY;
                        const float factor = len / total_layer_length;
                        if (transition_out)
                        {
                            // The ramp-down pass retraces this move on its original XY at the top Z, so the line
                            // is cloned before the taper, the Z ramp and the XY blend; it extrudes what the
                            // slope left out.
                            GCodeReader::GCodeLine ramp_line(line);
                            ramp_line.set(reader, E, line.e() * (1.f - factor), 5);
                            ramp_gcode += ramp_line.raw() + '\n';
                        }
                        if (transition_in)
                            // Transition layer, interpolate the amount of extrusion from zero to the final value.
                            line.set(reader, E, line.e() * factor, 5);

                        // This line is the core of Spiral Vase mode, ramp up the Z smoothly
                        line.set(reader, Z, z + factor * layer_height);

                        bool emit_gcode_line = true;
                        if (smooth_spiral)
                        {
                            // Now we also need to try to interpolate X and Y
                            Vec2f p(line.x(), line.y());   // Get current x/y coordinates
                            current_layer.emplace_back(p); // Store that point for later use on the next layer

                            auto [nearest_distance, idx, nearest_pt] =
                                previous_layer_distancer.distance_from_lines_extra<false>(p.cast<double>());
                            if (nearest_distance < max_xy_smoothing)
                            {
                                // Interpolate between the point on this layer and the point on the previous layer
                                Vec2f target = nearest_pt.cast<float>() * (1.f - factor) + p * factor;

                                // We will emit a new g-code line only when XYZ positions differ from the previous g-code line.
                                emit_gcode_line = GCodeFormatter::quantize(last_point) !=
                                                  GCodeFormatter::quantize(target);

                                line.set(reader, X, target.x());
                                line.set(reader, Y, target.y());
                                // We need to figure out the distance of this new line!
                                float modified_dist_XY = (last_point - target).norm();
                                // Scale the extrusion amount according to change in length
                                line.set(reader, E, line.e() * modified_dist_XY / dist_XY, 5);
                                last_point = target;
                            }
                            else
                            {
                                last_point = p;
                            }
                        }

                        if (emit_gcode_line)
                            new_gcode += line.raw() + '\n';
                    }
                    else if (dist_XY > max_xy_smoothing && !line.has_e())
                    {
                        // A travel longer than the blend radius cannot be blended into the previous layer: the
                        // loop's start moved that far because a region of the part ended. It is kept, at the
                        // previous layer's top where the nozzle is, and the blend restarts from its end.
                        DBG_COUNT("SPIRAL_TRAVEL_KEPT");
                        new_gcode += line.raw() + '\n';
                        last_point = Vec2f(line.new_X(reader), line.new_Y(reader));
                    }
                    return;
                    /*  Skip travel moves: the move to first perimeter point will
                    cause a visible seam when loops are not aligned in XY; by skipping
                    it we blend the first loop move in the XY plane (although the smoothness
                    of such blend depend on how long the first segment is; maybe we should
                    enforce some minimum length?).
                    When smooth_spiral is enabled, we're gonna end up exactly where the next layer should
                    start anyway, so we don't need the travel move */
                }

                // A G1 without X, Y or Z: a retract, an unretract or a speed change. The pass keeps only the
                // speed lines, which carry the cooling buffer's block markers.
                new_gcode += line.raw() + '\n';
                if (transition_out && line.has_f() && !line.has_e() && !line.has_unknown_axis())
                    ramp_gcode += line.raw() + '\n';
                return;
            }

            new_gcode += line.raw() + '\n';
            if (transition_out && ramp_pass_keeps_line(line))
                ramp_gcode += line.raw() + '\n';
        });

    m_previous_layer = std::move(current_layer);
    m_spiral_live = true;
    if (last_layer)
        return new_gcode + ramp_gcode;
    m_pending_ramp = std::move(ramp_gcode);
    return new_gcode;
}

} // namespace Luminary
