///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2022 Lukas Matena @lukasmatena, Tomas Meszaros @tamasmeszaros
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <array>
#include <string>
#include <vector>
#include <utility>
#include <optional>

struct stl_facet;

namespace Luminary
{

struct OCCTVolume
{
    std::string volume_name;
    std::vector<stl_facet> facets;
};

struct OCCTResult
{
    std::string error_str;
    std::string warning_str;
    std::string object_name;
    std::vector<OCCTVolume> volumes;
};

using LoadStepFn = bool (*)(const char *path, OCCTResult *occt_result,
                            std::optional<std::pair<double, double>> deflections);

}; // namespace Luminary
