///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2023 - 2023
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <string>

namespace Luminary
{

class Model;
// Load an SVG file as embossed shape into a provided model.
bool load_svg(const std::string &input_file, Model &output_model);

}; // namespace Luminary
