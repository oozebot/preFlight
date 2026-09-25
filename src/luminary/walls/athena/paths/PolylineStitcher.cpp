///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) 2022 Ultimaker B.V. - CuraEngine
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/

#include "PolylineStitcher.hpp"

#include "ExtrusionLine.hpp"
#include "luminary/geometry/index/athena/PolygonsPointIndex.hpp"
#include "luminary/geometry/contours/Polygon.hpp"

namespace Luminary
{
namespace Athena
{
struct ExtrusionJunction;
} // namespace Athena
} // namespace Luminary

namespace Luminary::Athena
{

template<>
bool PolylineStitcher<VariableWidthLines, ExtrusionLine, ExtrusionJunction>::canReverse(
    const PathsPointIndex<VariableWidthLines> &ppi)
{
    if ((*ppi.polygons)[ppi.poly_idx].is_odd)
        return true;
    else
        return false;
}

template<>
bool PolylineStitcher<Polygons, Polygon, Point>::canReverse(const PathsPointIndex<Polygons> &)
{
    return true;
}

template<>
bool PolylineStitcher<VariableWidthLines, ExtrusionLine, ExtrusionJunction>::canConnect(const ExtrusionLine &a,
                                                                                        const ExtrusionLine &b)
{
    // source_poly_id is tagged on ExtrusionLines but NOT gated here. Gating on
    // source_poly_id breaks 0-width contour markers, interlocking perimeters,
    // and gap-fill that must stitch across polygon boundaries. Future use requires
    // exempting markers and interlocking paths.
    //
    // We also intentionally allow stitching between pure-marker lines (all junctions
    // w==0) and real-bead lines (all junctions w>0). When the skeletal trapezoidation
    // produces fragmented marker output (open polylines on geometry where the depth-N+1
    // medial axis can't close into polygons), this loose stitching closes them into
    // mixed-width loops. separateOutInnerContour handles those mixed loops correctly:
    // it drops them from toolpaths (so the bead portions don't emit as visible extrusion
    // across empty space) but keeps their closed polygons in
    // inner_contour to preserve the infill zone. Forbidding marker-bead stitching here
    // would prevent that closure and cause empty inner_contour on geometry where the
    // marker pass is fragile (the bucket repro at z=34.90).
    return a.is_odd == b.is_odd;
}

template<>
bool PolylineStitcher<Polygons, Polygon, Point>::canConnect(const Polygon &, const Polygon &)
{
    return true;
}

template<>
bool PolylineStitcher<VariableWidthLines, ExtrusionLine, ExtrusionJunction>::isOdd(const ExtrusionLine &line)
{
    return line.is_odd;
}

template<>
bool PolylineStitcher<Polygons, Polygon, Point>::isOdd(const Polygon &)
{
    return false;
}

} // namespace Luminary::Athena
