///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2020 - 2023 Enrico Turri @enricoturri1966
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <../../src/viewer/include/Viewer.hpp>
#include <../../src/viewer/include/PathVertex.hpp>
#include <../../src/viewer/include/GCodeInputData.hpp>
#include <../../src/viewer/include/ColorRange.hpp>
#include <stddef.h>
#include <string>
#include <vector>
#include <cstddef>

#include "../../src/viewer/include/Types.hpp"
#include "luminary/colour/rgb/Color.hpp"
#include "luminary/gcode/interpret/GCodeProcessor.hpp"
#include "DSKY/GUI/GUI_Preview.hpp"
#include "luminary/toolpath/extrusion/ExtrusionRole.hpp"
#include "luminary/geometry/primitives/Point.hpp"

namespace Luminary
{
class Print;

namespace CustomGCode
{
struct Item;
} // namespace CustomGCode
} // namespace Luminary

namespace libvgcode
{
class Viewer;

// mapping from Luminary::Vec3f to libvgcode::Vec3
extern Vec3 convert(const Luminary::Vec3f &v);

// mapping from libvgcode::Vec3 to Luminary::Vec3f
extern Luminary::Vec3f convert(const Vec3 &v);

// mapping from Luminary::Matrix4f to libvgcode::Mat4x4
extern Mat4x4 convert(const Luminary::Matrix4f &m);

// mapping from libvgcode::Color to Luminary::ColorRGBA
extern Luminary::ColorRGBA convert(const Color &c);

// mapping from Luminary::ColorRGBA to libvgcode::Color
extern Color convert(const Luminary::ColorRGBA &c);

// mapping from encoded color to libvgcode::Color
extern Color convert(const std::string &color_str);

// mapping from libvgcode::EGCodeExtrusionRole to Luminary::GCodeExtrusionRole
extern Luminary::GCodeExtrusionRole convert(EGCodeExtrusionRole role);

// mapping from Luminary::GCodeExtrusionRole to libvgcode::EGCodeExtrusionRole
extern EGCodeExtrusionRole convert(Luminary::GCodeExtrusionRole role);

// mapping from Luminary::EMoveType to libvgcode::EMoveType
extern EMoveType convert(Luminary::EMoveType type);

// mapping from DSKY::Preview::OptionType to libvgcode::EOptionType
extern EOptionType convert(const DSKY::Preview::OptionType &type);

// mapping from Luminary::PrintEstimatedStatistics::ETimeMode to libvgcode::ETimeMode
extern ETimeMode convert(const Luminary::PrintEstimatedStatistics::ETimeMode &mode);

// mapping from libvgcode::ETimeMode to Luminary::PrintEstimatedStatistics::ETimeMode
extern Luminary::PrintEstimatedStatistics::ETimeMode convert(const ETimeMode &mode);

// mapping from Luminary::GCodeProcessorResult to libvgcode::GCodeInputData
extern GCodeInputData convert(const Luminary::GCodeProcessorResult &result,
                              const std::vector<std::string> &str_tool_colors,
                              const std::vector<std::string> &str_color_print_colors, const Viewer &viewer,
                              std::function<void(float)> progress_callback = nullptr);

// mapping from Luminary::Print to libvgcode::GCodeInputData
extern GCodeInputData convert(const Luminary::Print &print, const std::vector<std::string> &str_tool_colors,
                              const std::vector<std::string> &str_color_print_colors,
                              const std::vector<Luminary::CustomGCode::Item> &color_print_values,
                              size_t extruders_count);

} // namespace libvgcode

