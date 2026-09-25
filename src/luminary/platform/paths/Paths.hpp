///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2016 - 2023 Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Filip Sykala @Jony01, David Kocík @kocikdav, Enrico Turri @enricoturri1966, Tomáš Mészáros @tamasmeszaros, Lukáš Matěna @lukasmatena, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2019 Sijmen Schoon
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <optional>
#include <string>

#include <boost/filesystem.hpp>

#if __APPLE__
//implemented at MacUtils.mm
std::string GetDataDir();
#endif //__APPLE__

namespace Luminary
{

// Set a path with GUI resource files.
void set_var_dir(const std::string &path);
// Return a full path to the GUI resource files.
const std::string &var_dir();
// Return a full resource path for a file_name.
std::string var(const std::string &file_name);

// Set a path with various static definition data (for example the initial config bundles).
void set_resources_dir(const std::string &path);
// Return a full path to the resources directory.
const std::string &resources_dir();

// Set a path with GUI localization files.
void set_local_dir(const std::string &path);
// Return a full path to the localization directory.
const std::string &localization_dir();

// Set a path with shapes gallery files.
void set_sys_shapes_dir(const std::string &path);
// Return a full path to the system shapes gallery directory.
const std::string &sys_shapes_dir();

// Return a full path to the custom shapes gallery directory.
std::string custom_shapes_dir();

// Set a path with shapes gallery files.
void set_custom_gcodes_dir(const std::string &path);
// Return a full path to the system shapes gallery directory.
const std::string &custom_gcodes_dir();

// Set a path with preset files.
void set_data_dir(const std::string &path);
// Return a full path to the GUI resource files.
const std::string &data_dir();
// Per-user, version-scoped directory for preprocessing-script Python packages
// (data_dir/python-packages/<major.minor>); lives outside the install so it survives upgrades.
std::string user_python_packages_dir();

// Format an output path for debugging purposes.
// Writes out the output path prefix to the console for the first time the function is called,
// so the user knows where to search for the debugging output.
std::string debug_out_path(const char *name, ...);

// The default data directory, read from the environment. The CLI hands the result to set_data_dir,
// so computing it and storing it are one responsibility.

// Only defined on linux.
std::optional<boost::filesystem::path> get_home_config_dir();
std::optional<boost::filesystem::path> get_home_local_dir();

std::string get_default_datadir();

} // namespace Luminary
