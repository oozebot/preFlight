///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2017 - 2021 Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966
///|/
///|/ Copyright (c) Prusa Research 2017 Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2012 - 2015 Alessandro Ranellucci @alranel
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

namespace Luminary
{

class Model;
class DynamicPrintConfig;
// load_amf takes one by pointer.
struct ConfigSubstitutionContext;

// Load the content of an amf file into the given model and configuration.
extern bool load_amf(const char *path, DynamicPrintConfig *config, ConfigSubstitutionContext *config_substitutions,
                     Model *model, bool check_version);

// Function to save AMF has been removed in 2.9.0, we no longer support saving data in AMF format.
// The option has been missing in the UI since 2.4.0 (except for CLI).

} // namespace Luminary
