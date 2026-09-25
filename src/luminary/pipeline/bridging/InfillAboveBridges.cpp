///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include <tcbspan/span.hpp>
#include "InfillAboveBridges.hpp"
#include "luminary/geometry/clipper/ClipperUtils.hpp"

namespace Luminary::PrepareInfill
{
void mark_as_infill_above_bridge(const ExPolygons &marker, const SurfaceRefsByRegion &layer)
{
    for (const SurfaceCollectionRef &region : layer)
    {
        const SurfacesPtr solid_surfaces = region.get().filter_by_types({stInternalSolid, stBridgeAnchor});
        const ExPolygons intersection{intersection_ex(solid_surfaces, marker, ApplySafetyOffset::No)};
        if (intersection.empty())
        {
            continue;
        }
        const ExPolygons clipped{diff_ex(solid_surfaces, marker, ApplySafetyOffset::Yes)};
        region.get().remove_type(stInternalSolid);
        region.get().remove_type(stBridgeAnchor);
        region.get().append(clipped, stInternalSolid);
        region.get().append(intersection, stSolidOverBridge);
    }
}

void separate_infill_above_bridges(const SurfaceRefs &surfaces, const double expand_offset)
{
    if (surfaces.empty())
    {
        return;
    }

    const SurfaceRefsByRegion *previous_layer{&surfaces.front()};
    for (const SurfaceRefsByRegion &layer : tcb::span{surfaces}.subspan(1))
    {
        ExPolygons bridges;
        for (const SurfaceCollectionRef &region : *previous_layer)
        {
            for (const Surface *bridge : region.get().filter_by_type(stBottomBridge))
            {
                bridges.push_back(bridge->expolygon);
            }
        }
        // No bridge below: nothing to mark, and the clip against an empty marker is a no-op.
        if (bridges.empty())
        {
            previous_layer = &layer;
            continue;
        }
        if (expand_offset > 0)
        {
            bridges = offset_ex(bridges, scale_(expand_offset));
        }
        mark_as_infill_above_bridge(bridges, layer);
        previous_layer = &layer;
    }
}
} // namespace Luminary::PrepareInfill
