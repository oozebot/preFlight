///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "Theme.hpp"
#include "imgui/imgui.h"

namespace Slic3r
{
namespace GUI
{
namespace Theme
{

ImVec4 Primary::GetImGuiColor(float alpha)
{
    return ImVec4(R_NORM, G_NORM, B_NORM, alpha);
}

ImVec4 Secondary::GetImGuiColor(float alpha)
{
    return ImVec4(R_NORM, G_NORM, B_NORM, alpha);
}

ImVec4 PrimaryDark::GetImGuiColor(float alpha)
{
    return ImVec4(R_NORM, G_NORM, B_NORM, alpha);
}

ImVec4 Complementary::GetImGuiColor(float alpha)
{
    return ImVec4(R_NORM, G_NORM, B_NORM, alpha);
}

} // namespace Theme
} // namespace GUI
} // namespace Slic3r
