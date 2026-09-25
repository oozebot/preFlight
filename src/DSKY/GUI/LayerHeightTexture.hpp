///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2016 - 2023 Vojtěch Bubník @bubnikv, Enrico Turri @enricoturri1966, David Kocík @kocikdav, Lukáš Matěna @lukasmatena, Oleksandra Iushchenko @YuSanka
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <vector>

#include "luminary/core/Prelude.hpp"
#include "luminary/model/slicing/Slicing.hpp"

namespace DSKY
{

// Produce a 1D texture packed into a 2D texture describing in the RGBA format
// the planned object layers.
// Returns number of cells used by the texture of the 0th LOD level.
// coordf_t is a global alias in the engine prelude, not a member of the engine namespace.
int generate_layer_height_texture(const Luminary::SlicingParameters &slicing_params,
                                  const std::vector<coordf_t> &layers, void *data, int rows, int cols,
                                  bool level_of_detail_2nd_level);

} // namespace DSKY
