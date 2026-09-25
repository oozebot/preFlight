///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2023 Vojtěch Bubník @bubnikv
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <functional>
#include <vector>

#include "luminary/supports/baobab/BaobabSupport.hpp"
#include "luminary/supports/backend/SupportCommon.hpp"
#include "luminary/supports/tree/TreeSupport.hpp"
#include "luminary/supports/model/SupportLayer.hpp"

namespace Luminary
{

class PrintObject;

namespace FFFTreeSupport
{

class TreeModelVolumes;
class InterfacePlacer;
struct TreeSupportSettings;

// Organic specific: Smooth branches and produce one cummulative mesh to be sliced.
void organic_draw_branches(PrintObject &print_object, TreeModelVolumes &volumes, const TreeSupportSettings &config,
                           std::vector<SupportElements> &move_bounds,

                           // I/O:
                           SupportGeneratorLayersPtr &bottom_contacts, SupportGeneratorLayersPtr &top_contacts,
                           InterfacePlacer &interface_placer,

                           // Output:
                           SupportGeneratorLayersPtr &intermediate_layers, SupportGeneratorLayerStorage &layer_storage,

                           std::function<void()> throw_on_cancel,
                           // Areas to exclude from organic support (e.g., snug/grid support regions)
                           const std::vector<Polygons> &excluded_areas = {},
                           // Per-layer overhang areas: the canopy's authority when interface layers
                           // are disabled and no contact layer exists to clip against.
                           const std::vector<Polygons> &baobab_overhangs = {},
                           // Out: per-layer canopy ring footprints keyed by print z in microns, so
                           // the toolpath generator can tell a canopy from a trunk.
                           BaobabCanopyFootprints *baobab_canopy_footprints = nullptr);

} // namespace FFFTreeSupport

} // namespace Luminary