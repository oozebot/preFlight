///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <string>

namespace Luminary::Utils
{

bool verify_exp(const std::string &token);
int get_exp_seconds(const std::string &token);

} // namespace Luminary::Utils
