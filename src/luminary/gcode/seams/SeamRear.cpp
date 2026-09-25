///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "SeamRear.hpp"

#include <algorithm>
#include <optional>
#include <utility>

#include "luminary/geometry/index/AABBTreeLines.hpp"
#include "luminary/geometry/primitives/BoundingBox.hpp"
#include "SeamAligned.hpp"
#include "SeamChoice.hpp"
#include "SeamPerimeters.hpp"
#include "SeamShells.hpp"

namespace Luminary::Seams::Rear
{
using Perimeters::PointType;
using Perimeters::PointClassification;

namespace Impl
{

BoundingBoxf get_bounding_box(const Shells::Shell<> &shell)
{
    BoundingBoxf result;
    for (const Shells::Slice<> &slice : shell)
    {
        result.merge(BoundingBoxf{slice.boundary.positions});
    }
    return result;
}

std::optional<SeamChoice> get_clear_max_y_corner(const std::vector<PerimeterLine> &possible_lines,
                                                 const Perimeters::Perimeter &perimeter, const SeamChoice &max_y_choice,
                                                 const double rear_tolerance)
{
    if (perimeter.angle_types[max_y_choice.previous_index] != Perimeters::AngleType::concave)
    {
        return std::nullopt;
    }

    const double epsilon{1e-2};

    // Check if there are two max y corners (e.g. on a cube).
    for (const PerimeterLine &line : possible_lines)
    {
        if (line.previous_index != max_y_choice.previous_index &&
            perimeter.angle_types[line.previous_index] == Perimeters::AngleType::concave &&
            max_y_choice.position.y() < line.a.y() + epsilon && (max_y_choice.position - line.a).norm() > epsilon)
        {
            return std::nullopt;
        }
        if (line.next_index != max_y_choice.next_index &&
            perimeter.angle_types[line.next_index] == Perimeters::AngleType::concave &&
            max_y_choice.position.y() < line.b.y() + epsilon && (max_y_choice.position - line.b).norm() > epsilon)
        {
            return std::nullopt;
        }
    }

    return max_y_choice;
}

SeamChoice get_max_y_choice(const std::vector<PerimeterLine> &possible_lines)
{
    if (possible_lines.empty())
    {
        throw std::runtime_error{"No possible lines!"};
    }

    Vec2d point{possible_lines.front().a};
    std::size_t point_index{possible_lines.front().previous_index};

    for (const PerimeterLine &line : possible_lines)
    {
        if (line.a.y() > point.y())
        {
            point = line.a;
            point_index = line.previous_index;
        }
        if (line.b.y() > point.y())
        {
            point = line.b;
            point_index = line.next_index;
        }
    }

    return SeamChoice{point_index, point_index, point};
}

SeamChoice get_nearest(const AABBTreeLines::LinesDistancer<PerimeterLine> &distancer, const Vec2d point)
{
    const auto [_, line_index, resulting_point] = distancer.distance_from_lines_extra<false>(point);
    return SeamChoice{distancer.get_lines()[line_index].previous_index, distancer.get_lines()[line_index].next_index,
                      resulting_point};
}

struct RearestPointCalculator
{
    double rear_tolerance;
    double rear_y_offset;
    BoundingBoxf bounding_box;

    std::optional<SeamChoice> operator()(const Perimeters::Perimeter &perimeter, const PointType point_type,
                                         const PointClassification point_classification)
    {
        std::vector<PerimeterLine> possible_lines;
        for (std::size_t i{0}; i < perimeter.positions.size(); ++i)
        {
            const std::size_t next_index{i == perimeter.positions.size() - 1 ? 0 : i + 1};
            if (perimeter.point_types[i] != point_type)
            {
                continue;
            }
            if (perimeter.point_classifications[i] != point_classification)
            {
                continue;
            }
            if (perimeter.point_types[next_index] != point_type)
            {
                continue;
            }
            if (perimeter.point_classifications[next_index] != point_classification)
            {
                continue;
            }
            possible_lines.push_back(
                PerimeterLine{perimeter.positions[i], perimeter.positions[next_index], i, next_index});
        }
        if (possible_lines.empty())
        {
            return std::nullopt;
        }

        const SeamChoice max_y_choice{get_max_y_choice(possible_lines)};

        if (const auto clear_max_y_corner{
                get_clear_max_y_corner(possible_lines, perimeter, max_y_choice, rear_tolerance)})
        {
            return *clear_max_y_corner;
        }

        const BoundingBoxf bounding_box{perimeter.positions};
        const AABBTreeLines::LinesDistancer<PerimeterLine> possible_distancer{possible_lines};
        const double center_x{(bounding_box.max.x() + bounding_box.min.x()) / 2.0};
        const Vec2d prefered_position{center_x, bounding_box.max.y() + rear_y_offset};
        auto [_, line_index, point] = possible_distancer.distance_from_lines_extra<false>(prefered_position);
        const Vec2d location_at_bb{center_x, bounding_box.max.y()};
        auto [_d, line_index_at_bb, point_bb] = possible_distancer.distance_from_lines_extra<false>(location_at_bb);
        const double y_distance{point.y() - point_bb.y()};

        SeamChoice result{possible_lines[line_index].previous_index, possible_lines[line_index].next_index, point};

        if (y_distance < 0)
        {
            result = get_nearest(possible_distancer, point_bb);
        }
        else if (y_distance <= rear_tolerance)
        {
            const double factor{y_distance / rear_tolerance};
            result = get_nearest(possible_distancer, factor * point + (1 - factor) * point_bb);
        }

        if (bounding_box.max.y() - result.position.y() > rear_tolerance)
        {
            return max_y_choice;
        }

        return result;
    }
};
} // namespace Impl

std::vector<std::vector<SeamPerimeterChoice>> get_object_seams(
    std::vector<std::vector<Perimeters::BoundedPerimeter>> &&perimeters,
    const std::vector<std::vector<std::optional<Vec2d>>> &columns, const double rear_tolerance,
    const double rear_y_offset)
{
    std::vector<std::vector<SeamPerimeterChoice>> result;
    // The last painted seam: a painted line is followed up the object by picking the run nearest it.
    std::optional<Vec2d> previous_painted;

    for (std::size_t layer_index{0}; layer_index < perimeters.size(); ++layer_index)
    {
        std::vector<Perimeters::BoundedPerimeter> &layer{perimeters[layer_index]};
        result.emplace_back();
        for (std::size_t perimeter_index{0}; perimeter_index < layer.size(); ++perimeter_index)
        {
            Perimeters::BoundedPerimeter &perimeter{layer[perimeter_index]};
            const std::optional<Vec2d> column{layer_index < columns.size() &&
                                                      perimeter_index < columns[layer_index].size()
                                                  ? columns[layer_index][perimeter_index]
                                                  : std::nullopt};
            if (column)
            {
                // On the painted column, the centerline filtered across the layers.
                result.back().push_back(
                    SeamPerimeterChoice{Aligned::choose_column_seam(perimeter.perimeter, *column, rear_tolerance),
                                        std::move(perimeter.perimeter)});
                previous_painted = column;
            }
            else if (perimeter.perimeter.is_degenerate)
            {
                std::optional<Seams::SeamChoice> seam_choice{Seams::choose_degenerate_seam_point(perimeter.perimeter)};
                if (seam_choice)
                {
                    result.back().push_back(SeamPerimeterChoice{*seam_choice, std::move(perimeter.perimeter)});
                }
                else
                {
                    result.back().push_back(SeamPerimeterChoice{SeamChoice{}, std::move(perimeter.perimeter)});
                }
            }
            else
            {
                BoundingBoxf bounding_box{unscaled(perimeter.bounding_box)};
                // A painted perimeter seams on the paint centerline, not at the rearmost painted
                // point: that sat on the stroke's edge and followed the edge's flicker. Without a
                // painted seam to follow, the run nearest the rear centre is the one meant.
                const Vec2d reference{previous_painted ? *previous_painted
                                                       : Vec2d{(bounding_box.min.x() + bounding_box.max.x()) / 2.0,
                                                               bounding_box.max.y()}};
                std::optional<SeamChoice> seam_choice{
                    Aligned::Impl::choose_painted_seam(perimeter.perimeter, reference, rear_tolerance)};
                if (seam_choice)
                {
                    previous_painted = seam_choice->position;
                }
                else
                {
                    seam_choice = Seams::choose_seam_point(perimeter.perimeter,
                                                           Impl::RearestPointCalculator{rear_tolerance, rear_y_offset,
                                                                                        bounding_box});
                }
                result.back().push_back(SeamPerimeterChoice{*seam_choice, std::move(perimeter.perimeter)});
            }
        }
    }

    return result;
}
} // namespace Luminary::Seams::Rear
