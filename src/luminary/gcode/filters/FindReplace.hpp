///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2022 Vojtěch Bubník @bubnikv
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <boost/regex.hpp>
#include <boost/regex/v5/regex.hpp>
#include <string>
#include <vector>

#include "luminary/config/catalog/PrintConfig.hpp"

namespace Luminary
{

class GCodeFindReplace
{
public:
    GCodeFindReplace(const PrintConfig &print_config) : GCodeFindReplace(print_config.gcode_substitutions.values) {}
    GCodeFindReplace(const std::vector<std::string> &gcode_substitutions);

    std::string process_layer(const std::string &gcode);

private:
    struct Substitution
    {
        std::string plain_pattern;
        boost::regex regexp_pattern;
        std::string format;

        bool regexp{false};
        bool case_insensitive{false};
        bool whole_word{false};
        // Valid for regexp only. Equivalent to Perl's /s modifier.
        bool single_line{false};
    };
    std::vector<Substitution> m_substitutions;
};

} // namespace Luminary
