///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2018 - 2021 Vojtěch Bubník @bubnikv
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <string>

#include "luminary/core/Prelude.hpp"
#include "luminary/config/catalog/PrintConfig.hpp"

namespace Luminary
{

// Run post processing script / scripts if defined.
// Returns true if a post-processing script was executed.
// Returns false if no post-processing script was defined.
// Throws an exception on error.
// host is one of "File", "PrusaLink", "Repetier", "OctoPrint", "FlashAir", "Duet", "AstroBox" ...
// If make_copy, then a temp file will be created for src_path by adding a ".pp" suffix and src_path will be updated.
// In that case the caller is responsible to delete the temp file created.
// output_name is the final name of the G-code on SD card or when uploaded to PrusaLink or OctoPrint.
// If uploading to PrusaLink or OctoPrint, then the file will be renamed to output_name first on the target host.
// The post-processing script may change the output_name.
extern bool run_post_process_scripts(std::string &src_path, bool make_copy, const std::string &host,
                                     std::string &output_name, const DynamicPrintConfig &config);

inline bool run_post_process_scripts(std::string &src_path, const DynamicPrintConfig &config)
{
    std::string src_path_name = src_path;
    return run_post_process_scripts(src_path, false, "File", src_path_name, config);
}

} // namespace Luminary
