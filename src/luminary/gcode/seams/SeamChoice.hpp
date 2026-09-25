///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <utility>

#include "luminary/geometry/contours/Polygon.hpp"
#include "SeamShells.hpp"
#include "luminary/geometry/primitives/BoundingBox.hpp"
#include "SeamGeometry.hpp"
#include "SeamPerimeters.hpp"
#include "luminary/geometry/primitives/Point.hpp"

namespace Luminary::Seams
{
using SeamChoice = Perimeters::PointOnPerimeter;

struct SeamPerimeterChoice
{
    SeamPerimeterChoice(const SeamChoice &choice, Perimeters::Perimeter &&perimeter)
        : choice(choice)
        , perimeter(std::move(perimeter))
        , bounding_box(Polygon{Geometry::scaled(this->perimeter.positions)}.bounding_box())
    {
    }

    SeamChoice choice;
    Perimeters::Perimeter perimeter;
    BoundingBox bounding_box;
};

using SeamPicker = std::function<std::optional<SeamChoice>(const Perimeters::Perimeter &, const Perimeters::PointType,
                                                           const Perimeters::PointClassification)>;

std::optional<SeamChoice> maybe_choose_seam_point(const Perimeters::Perimeter &perimeter,
                                                  const SeamPicker &seam_picker);

/**
 * Go throught points on perimeter and choose the best seam point closest to
 * the prefered position.
 *
 * Points in the perimeter can be diveded into 3x3=9 categories. An example category is
 * enforced overhanging point. These categories are searched in particualr order.
 * For example enforced overhang will be always choosen over common embedded point, etc.
 *
 * A closest point is choosen from the first non-empty category.
 */
SeamChoice choose_seam_point(const Perimeters::Perimeter &perimeter, const SeamPicker &seam_picker);

std::optional<SeamChoice> choose_degenerate_seam_point(const Perimeters::Perimeter &perimeter);

} // namespace Luminary::Seams
