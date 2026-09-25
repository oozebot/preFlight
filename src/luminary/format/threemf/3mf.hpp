///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2018 - 2022 Enrico Turri @enricoturri1966, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Tomáš Mészáros @tamasmeszaros
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include "luminary/core/Semver.hpp"
#include <boost/optional/optional.hpp>
#include <optional>
#include <string>
namespace Luminary
{

class Model;
struct ConfigSubstitutionContext;
class DynamicPrintConfig;
struct ThumbnailData;

// Info extracted from a 3mf file's metadata without fully loading it.
struct ProjectFileInfo
{
    bool has_config = false;                 // contains Slic3r_PE.config
    std::optional<Semver> generator_version; // preFlight version, if the file was saved by preFlight
    std::string generator_application;       // raw Application metadata tag (e.g. "OrcaSlicer-2.3.1")
};

// Inspect 3mf archive headers to determine origin and whether it contains config.
extern ProjectFileInfo is_project_3mf(const std::string &);

// Load the content of a 3mf file into the given model and preset bundle.
extern bool load_3mf(const char *path, DynamicPrintConfig &config, ConfigSubstitutionContext &config_substitutions,
                     Model *model, bool check_version, boost::optional<Semver> &generator_version,
                     std::string *generator_application = nullptr, bool *scripts_suppressed = nullptr,
                     bool *post_process_stripped = nullptr);

// Save the given model and the config data contained in the given Print into a 3mf file.
// The model could be modified during the export process if meshes are not repaired or have no shared vertices
extern bool store_3mf(const char *path, Model *model, const DynamicPrintConfig *config, bool fullpath_sources,
                      const ThumbnailData *thumbnail_data = nullptr, bool zip64 = true);

} // namespace Luminary
