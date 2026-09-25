///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include <string>
#include <unordered_map>

namespace Luminary
{

// OrcaSlicer uses different names for many of the same settings. This map lets the
// PlaceholderParser resolve Orca G-code placeholder names to their preFlight equivalents
// at runtime, so imported Orca custom G-code works in both [bracket] and {brace} syntax
// without any string conversion at import time.
const std::unordered_map<std::string, std::string> &orca_gcode_aliases();

} // namespace Luminary
