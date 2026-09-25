///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <vector>

#include "luminary/walls/arachne/paths/ExtrusionLine.hpp"
#include "luminary/walls/athena/paths/ExtrusionLine.hpp"

namespace Luminary
{
class ExPolygon;
class Polyline;
class Polygon;
class PrintRegionConfig;

struct PerimeterRegion;

using ExPolygons = std::vector<ExPolygon>;
using PerimeterRegions = std::vector<PerimeterRegion>;
} // namespace Luminary

namespace Luminary::Arachne
{
struct ExtrusionLine;
}

namespace Luminary::Athena
{
struct ExtrusionLine;
}

namespace Luminary::Algorithm::LineSegmentation
{

struct PolylineSegment
{
    Polyline polyline;
    size_t clip_idx;
};

struct PolylineRegionSegment
{
    Polyline polyline;
    // Pointer is safe because configs live in PerimeterRegions which outlive segments
    const PrintRegionConfig *config;

    PolylineRegionSegment(const Polyline &polyline, const PrintRegionConfig &config)
        : polyline(polyline), config(&config)
    {
    }
    PolylineRegionSegment(Polyline &&polyline, const PrintRegionConfig &config)
        : polyline(std::move(polyline)), config(&config)
    {
    }
};

struct ExtrusionSegment
{
    Arachne::ExtrusionLine extrusion;
    size_t clip_idx;
};

struct ExtrusionRegionSegment
{
    Arachne::ExtrusionLine extrusion;
    const PrintRegionConfig *config;

    ExtrusionRegionSegment(const Arachne::ExtrusionLine &extrusion, const PrintRegionConfig &config)
        : extrusion(extrusion), config(&config)
    {
    }
    ExtrusionRegionSegment(Arachne::ExtrusionLine &&extrusion, const PrintRegionConfig &config)
        : extrusion(std::move(extrusion)), config(&config)
    {
    }
};

struct AthenaExtrusionSegment
{
    Athena::ExtrusionLine extrusion;
    size_t clip_idx;
};

struct AthenaExtrusionRegionSegment
{
    Athena::ExtrusionLine extrusion;
    const PrintRegionConfig *config;

    AthenaExtrusionRegionSegment(const Athena::ExtrusionLine &extrusion, const PrintRegionConfig &config)
        : extrusion(extrusion), config(&config)
    {
    }
    AthenaExtrusionRegionSegment(Athena::ExtrusionLine &&extrusion, const PrintRegionConfig &config)
        : extrusion(std::move(extrusion)), config(&config)
    {
    }
};

using PolylineSegments = std::vector<PolylineSegment>;
using ExtrusionSegments = std::vector<ExtrusionSegment>;
using ExtrusionRegionSegments = std::vector<ExtrusionRegionSegment>;
using PolylineRegionSegments = std::vector<PolylineRegionSegment>;

using AthenaExtrusionSegments = std::vector<AthenaExtrusionSegment>;
using AthenaExtrusionRegionSegments = std::vector<AthenaExtrusionRegionSegment>;

PolylineSegments polyline_segmentation(const Polyline &subject, const std::vector<ExPolygons> &expolygons_clips,
                                       size_t default_clip_idx = 0);
PolylineSegments polygon_segmentation(const Polygon &subject, const std::vector<ExPolygons> &expolygons_clips,
                                      size_t default_clip_idx = 0);

ExtrusionSegments extrusion_segmentation(const Arachne::ExtrusionLine &subject,
                                         const std::vector<ExPolygons> &expolygons_clips, size_t default_clip_idx = 0);
ExtrusionRegionSegments extrusion_segmentation(const Arachne::ExtrusionLine &subject,
                                               const PrintRegionConfig &base_config,
                                               const PerimeterRegions &perimeter_regions_clips);

PolylineRegionSegments polyline_segmentation(const Polyline &subject, const PrintRegionConfig &base_config,
                                             const PerimeterRegions &perimeter_regions_clips);
PolylineRegionSegments polygon_segmentation(const Polygon &subject, const PrintRegionConfig &base_config,
                                            const PerimeterRegions &perimeter_regions_clips);

AthenaExtrusionSegments extrusion_segmentation(const Athena::ExtrusionLine &subject,
                                               const std::vector<ExPolygons> &expolygons_clips,
                                               size_t default_clip_idx = 0);
AthenaExtrusionRegionSegments extrusion_segmentation(const Athena::ExtrusionLine &subject,
                                                     const PrintRegionConfig &base_config,
                                                     const PerimeterRegions &perimeter_regions_clips);

} // namespace Luminary::Algorithm::LineSegmentation
