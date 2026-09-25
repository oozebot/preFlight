///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "Theme.hpp"
#include "ThemePalette.hpp"
#include "imgui/imgui.h"

namespace DSKY
{

namespace Theme
{

// The brand-accent ImGui colors come from the active theme palette (defaulting to the preFlight
// orange), so every consumer of Theme::Primary/Secondary/PrimaryDark follows the theme.
static ImVec4 to_imvec4(const wxColour &c, float alpha)
{
    return ImVec4(c.Red() / 255.0f, c.Green() / 255.0f, c.Blue() / 255.0f, alpha);
}

ImVec4 Primary::GetImGuiColor(float alpha)
{
    return to_imvec4(active_palette().accent_primary, alpha);
}

ImVec4 Secondary::GetImGuiColor(float alpha)
{
    return to_imvec4(active_palette().accent_secondary, alpha);
}

ImVec4 PrimaryDark::GetImGuiColor(float alpha)
{
    return to_imvec4(active_palette().accent_dark, alpha);
}

ImVec4 Complementary::GetImGuiColor(float alpha)
{
    return ImVec4(R_NORM, G_NORM, B_NORM, alpha);
}

} // namespace Theme
} // namespace DSKY
