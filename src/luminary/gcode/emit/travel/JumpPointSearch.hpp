///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2022 - 2023 Pavel Mikuš @Godrak
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <cstdint>
#include <vector>

#include "luminary/geometry/primitives/BoundingBox.hpp"
#include "luminary/geometry/contours/Polygon.hpp"
#include "luminary/layer/model/Layer.hpp"
#include "luminary/geometry/primitives/Point.hpp"
#include "luminary/geometry/contours/Polyline.hpp"
#include "luminary/core/Prelude.hpp"
#include "luminary/geometry/primitives/Line.hpp"

namespace Luminary
{
class Layer;

class JPSPathFinder
{
    using Pixel = Point;
    // The obstacle pixels as a bitmap over max_search_box: the search reads millions of cells
    // per layer. add_obstacles records the pixel lines and grows the box from their pixels;
    // the first find_path after a change draws them into the bitmap.
    Lines obstacle_lines;
    std::vector<uint8_t> inpassable;
    bool inpassable_dirty = false;
    coordf_t print_z;
    BoundingBox max_search_box;
    Lines bed_shape;

    const coord_t resolution = scaled(1.5);
    Pixel pixelize(const Point &p) { return p / resolution; }
    Point unpixelize(const Pixel &p) { return p * resolution; }
    void build_inpassable();
    bool is_inpassable(const Pixel &p) const;

public:
    JPSPathFinder() = default;
    void init_bed_shape(const Points &bed_shape) { this->bed_shape = (to_lines(Polygon{bed_shape})); };
    void clear();
    void add_obstacles(const Lines &obstacles);
    void add_obstacles(const Layer *layer, const Point &global_origin);
    Polyline find_path(const Point &start, const Point &end);
};

} // namespace Luminary
