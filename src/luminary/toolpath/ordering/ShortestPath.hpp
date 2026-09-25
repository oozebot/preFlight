///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2019 - 2023 Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <stddef.h>
#include <utility>
#include <vector>
#include <cstddef>

#include "luminary/core/Prelude.hpp"
#include "luminary/toolpath/extrusion/ExtrusionEntity.hpp"
#include "luminary/geometry/primitives/Point.hpp"
#include "luminary/geometry/contours/ExPolygon.hpp"
#include "luminary/geometry/contours/Polyline.hpp"
// chain_clipper_polypaths is declared over PolyPaths, an alias this header cannot forward declare.
#include "luminary/geometry/clipper/ClipperUtils.hpp"

namespace Luminary
{
class ExtrusionEntityCollection;
class Line;

// Forward declarations removed - types are defined as aliases in ClipperUtils.hpp

class ExPolygon;
struct LayerIsland;

using ExPolygons = std::vector<ExPolygon>;

// Used by chain_expolygons()
std::vector<size_t> chain_points(const Points &points, const Point *start_near = nullptr);
// Used to give layer islands a print order.
std::vector<size_t> chain_expolygons(const ExPolygons &expolygons);

// Chain extrusion entities by a shortest distance. Returns the ordered extrusions together with a "reverse" flag.
// Set input "reversed" to true if the vector of "entities" is to be considered to be reversed once already.
std::vector<std::pair<size_t, bool>> chain_extrusion_entities(const std::vector<ExtrusionEntity *> &entities,
                                                              const Point *start_near = nullptr,
                                                              const bool reversed = false);
// Reorder & reverse extrusion entities in place based on the "chain" ordering.
void reorder_extrusion_entities(std::vector<ExtrusionEntity *> &entities,
                                const std::vector<std::pair<size_t, bool>> &chain);
// Reorder & reverse extrusion entities in place.
void chain_and_reorder_extrusion_entities(std::vector<ExtrusionEntity *> &entities, const Point *start_near = nullptr);

// Chain extrusion entities by a shortest distance. Returns the ordered extrusions together with a "reverse" flag.
// Set input "reversed" to true if the vector of "entities" is to be considered to be reversed.
ExtrusionEntityReferences chain_extrusion_references(const std::vector<ExtrusionEntity *> &entities,
                                                     const Point *start_near = nullptr, const bool reversed = false);
// The same as above, respect eec.no_sort flag.
ExtrusionEntityReferences chain_extrusion_references(const ExtrusionEntityCollection &eec,
                                                     const Point *start_near = nullptr, const bool reversed = false);

std::vector<std::pair<size_t, bool>> chain_extrusion_paths(std::vector<ExtrusionPath> &extrusion_paths,
                                                           const Point *start_near = nullptr);
void reorder_extrusion_paths(std::vector<ExtrusionPath> &extrusion_paths, std::vector<std::pair<size_t, bool>> &chain);
void chain_and_reorder_extrusion_paths(std::vector<ExtrusionPath> &extrusion_paths, const Point *start_near = nullptr);

Polylines chain_polylines(Polylines &&src, const Point *start_near = nullptr);
// Like chain_polylines but also returns the original index for each output polyline.
std::pair<Polylines, std::vector<size_t>> chain_polylines_with_indices(Polylines &&src,
                                                                       const Point *start_near = nullptr);
inline Polylines chain_polylines(const Polylines &src, const Point *start_near = nullptr)
{
    Polylines tmp(src);
    return chain_polylines(std::move(tmp), start_near);
}

PolyPaths chain_clipper_polypaths(const Points &points, const PolyPaths &items);

// Chain instances of print objects by an approximate shortest path.
// Returns pairs of PrintObject idx and instance of that PrintObject.
class Print;
struct PrintInstance;

std::vector<const PrintInstance *> chain_print_object_instances(const Print &print);

// Chain lines into polylines.
Polylines chain_lines(const std::vector<Line> &lines, const double point_distance_epsilon);

// Chain and reorder layer islands by the shortest distance.
std::vector<size_t> chain_layer_islands(const std::vector<std::reference_wrapper<const LayerIsland>> &islands,
                                        const Point *start_near = nullptr);
void reorder_layer_islands(std::vector<std::reference_wrapper<const LayerIsland>> &islands,
                           const std::vector<size_t> &chain);
void chain_and_reorder_layer_islands(std::vector<std::reference_wrapper<const LayerIsland>> &islands,
                                     const Point *start_near = nullptr);

} // namespace Luminary
