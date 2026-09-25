///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include <vector>

#include "luminary/geometry/contours/Polygon.hpp"

namespace Luminary::FFFTreeSupport
{

// The openings a tree branch may route through. The layer outlines are wound (CCW contours, CW
// holes), bottom layer first. A hole that cannot hold a disk of diameter min_opening is a narrow
// passage when it opens upward into free space: a screw hole through a floor, a blind hole in a
// top face, a slot in a lid. Such a passage is filled on every layer of its run, so a branch that
// looks for a way down never threads it. A narrow cavity that is closed above (the inside of a
// cap, a channel under a ceiling) is not a passage and stays open, so its ceiling keeps its
// support. A min_opening of 0 returns the outlines unchanged. Contours are never touched.
std::vector<Polygons> fill_narrow_passages(const std::vector<Polygons> &wound_outlines, coord_t min_opening);

} // namespace Luminary::FFFTreeSupport
