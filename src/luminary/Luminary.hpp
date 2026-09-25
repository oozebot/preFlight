///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include <string_view>

// Luminary is preFlight's engine: the code that decides what gets printed. Everything under
// src/luminary belongs to it and is reached from outside only by its module paths. Coordinates are
// coord_t, a signed integer count of nanometres of print space; scaled() converts millimetres to
// coord_t and unscaled() converts back, each with one definition, and both truncate toward zero.
namespace Luminary
{

// The library version, raised when a public type changes; the unit suite pins it.
inline constexpr int kVersionMajor = 0;
inline constexpr int kVersionMinor = 1;
std::string_view version_string() noexcept;

} // namespace Luminary
