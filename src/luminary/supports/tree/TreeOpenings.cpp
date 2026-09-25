///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "TreeOpenings.hpp"

#include "luminary/core/diagnostics/DebugCounters.hpp"
#include "luminary/geometry/clipper/ClipperUtils.hpp"
#include "luminary/geometry/primitives/BoundingBox.hpp"

namespace Luminary::FFFTreeSupport
{

namespace
{
struct NarrowHole
{
    size_t index;    // position in the layer's wound outline
    Polygon contour; // the hole wound as a contour
    Point probe;     // a point of the hole to test the layer above with
    double area;
    bool fill{false};
};

// Whether the model covers the point on a wound layer: inside a contour and outside every hole.
bool model_covers(const Polygons &wound, const Point &p)
{
    bool inside = false;
    for (const Polygon &poly : wound)
        if (poly.is_counter_clockwise() && poly.contains(p))
        {
            inside = true;
            break;
        }
    if (!inside)
        return false;
    for (const Polygon &poly : wound)
        if (!poly.is_counter_clockwise() && poly.contains(p))
            return false;
    return true;
}
} // namespace

std::vector<Polygons> fill_narrow_passages(const std::vector<Polygons> &wound_outlines, coord_t min_opening)
{
    if (min_opening <= 0)
        return wound_outlines;

    const size_t n = wound_outlines.size();
    std::vector<std::vector<NarrowHole>> narrow(n);
    // A hole holds a disk of the minimum opening exactly when shrinking it by the disk's radius
    // leaves something; a slot narrower than the opening vanishes whatever its length. A hole
    // whose bounding box or area cannot hold the disk is narrow without the offset, which keeps
    // a model with thousands of small holes cheap; the offset runs only on holes big enough.
    const float shrink = -0.5f * float(min_opening);
    constexpr double quarter_pi = 0.78539816339744831;
    const double disk_area = quarter_pi * double(min_opening) * double(min_opening);
    for (size_t l = 0; l < n; ++l)
        for (size_t i = 0; i < wound_outlines[l].size(); ++i)
        {
            const Polygon &poly = wound_outlines[l][i];
            if (poly.is_counter_clockwise())
                continue;
            Polygon as_contour = poly;
            as_contour.reverse();
            const double area = std::abs(as_contour.area());
            const Vec2crd extent = as_contour.bounding_box().size();
            const bool narrow_by_size = extent.x() < min_opening || extent.y() < min_opening || area < disk_area;
            if (!narrow_by_size && !offset(as_contour, shrink).empty())
                continue;
            NarrowHole hole;
            hole.index = i;
            hole.probe = as_contour.centroid();
            hole.area = area;
            hole.contour = std::move(as_contour);
            narrow[l].push_back(std::move(hole));
        }

    // Top down. A narrow hole continues the narrow hole above it when that one holds its probe
    // point and is of comparable size, and then shares its verdict. Otherwise the layer above
    // decides: free space over the hole makes it a passage (filled); the model, or a different
    // narrow hole, makes it a cavity a branch cannot enter from above (kept).
    size_t filled = 0;
    for (size_t l = n; l-- > 0;)
        for (NarrowHole &hole : narrow[l])
        {
            if (l + 1 >= n)
                hole.fill = true;
            else
            {
                const NarrowHole *above = nullptr;
                for (const NarrowHole &candidate : narrow[l + 1])
                    if (candidate.contour.contains(hole.probe))
                    {
                        above = &candidate;
                        break;
                    }
                if (above != nullptr)
                    hole.fill = above->area >= 0.5 * hole.area && above->area <= 2. * hole.area && above->fill;
                else
                    hole.fill = !model_covers(wound_outlines[l + 1], hole.probe);
            }
            if (hole.fill)
                ++filled;
        }

    std::vector<Polygons> out(n);
    for (size_t l = 0; l < n; ++l)
    {
        std::vector<bool> drop(wound_outlines[l].size(), false);
        for (const NarrowHole &hole : narrow[l])
            if (hole.fill)
                drop[hole.index] = true;
        out[l].reserve(wound_outlines[l].size());
        for (size_t i = 0; i < wound_outlines[l].size(); ++i)
            if (!drop[i])
                out[l].emplace_back(wound_outlines[l][i]);
    }
    if (filled > 0)
        DBG_COUNT_ADD("TREE_PASSAGE_FILLED", filled);
    return out;
}

} // namespace Luminary::FFFTreeSupport
