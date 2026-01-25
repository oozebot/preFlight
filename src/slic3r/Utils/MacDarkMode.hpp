///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2019 Vojtěch Bubník @bubnikv, Vojtěch Král @vojtechkral
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef slic3r_MacDarkMode_hpp_
#define slic3r_MacDarkMode_hpp_

#include <wx/event.h>

namespace Slic3r
{
namespace GUI
{

#if __APPLE__
extern bool mac_dark_mode();
extern double mac_max_scaling_factor();
#endif

} // namespace GUI
} // namespace Slic3r

#endif // MacDarkMode_h
