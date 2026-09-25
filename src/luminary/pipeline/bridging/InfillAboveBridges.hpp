///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <iostream>
#include <vector>
#include "luminary/geometry/surface/SurfaceCollection.hpp"

namespace Luminary::PrepareInfill
{
using SurfaceCollectionRef = std::reference_wrapper<SurfaceCollection>;
using SurfaceRefsByRegion = std::vector<SurfaceCollectionRef>;
using SurfaceRefs = std::vector<SurfaceRefsByRegion>;

void separate_infill_above_bridges(const SurfaceRefs &surfaces, const double expand_offset);
} // namespace Luminary::PrepareInfill
