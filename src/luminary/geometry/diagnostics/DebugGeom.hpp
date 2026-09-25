///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include <string>

#include "luminary/core/diagnostics/DebugOutput.hpp"
#include "luminary/geometry/contours/ExPolygon.hpp"
#include "luminary/geometry/contours/Polygon.hpp"
#include "luminary/geometry/contours/Polyline.hpp"

// Exact geometry out of the pipeline: with --debug-geom active, each call appends one
// WKT record (mm, unscaled, four decimals) to the sidecar next to the exported gcode:
//     G|<z>|<category.tag>|<attrs>|<WKT>
// No-op unless the sidecar is open, the category is enabled and z is inside the
// --debug-z window, so a dump site costs a mask check when off. `attrs` is free text
// (`key=value` pairs) for per-record fields such as an inset index.
namespace Luminary
{

std::string dbg_wkt(const Polygon &poly);
std::string dbg_wkt(const Polygons &polys);
std::string dbg_wkt(const ExPolygon &ep);
std::string dbg_wkt(const ExPolygons &eps);
std::string dbg_wkt(const Polyline &pl);
std::string dbg_wkt(const Polylines &pls);

void dbg_geom(uint32_t cat, double z, const char *tag, const Polygons &polys, const std::string &attrs = {});
void dbg_geom(uint32_t cat, double z, const char *tag, const ExPolygons &eps, const std::string &attrs = {});
void dbg_geom(uint32_t cat, double z, const char *tag, const Polyline &pl, const std::string &attrs = {});
void dbg_geom(uint32_t cat, double z, const char *tag, const Polylines &pls, const std::string &attrs = {});

// True when a dbg_geom call at this category and z would write, so callers can skip
// building geometry that would be discarded.
inline bool dbg_geom_active(uint32_t cat, double z)
{
    return g_geom_file != nullptr && debug_enabled(cat) && z_window_ok(z);
}

} // namespace Luminary
