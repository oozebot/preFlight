///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef slic3r_GUI_Theme_hpp_
#define slic3r_GUI_Theme_hpp_

#include <wx/colour.h>
#include <string>

// Forward declare ImVec4 to avoid including imgui.h here
struct ImVec4;

namespace Slic3r
{
namespace GUI
{
namespace Theme
{

// ============================================================================
// preFlight Brand Colors
// ============================================================================

// Primary Brand Color: #EAA032 (RGB: 234, 160, 50)
// This is the main orange color used throughout the application
namespace Primary
{
const wxColour WX_COLOR(234, 160, 50);
const std::string HEX = "#EAA032";
const std::string HEX_UPPER = "#EAA032";

// RGB values (0-255 range)
constexpr unsigned char R = 234;
constexpr unsigned char G = 160;
constexpr unsigned char B = 50;

// Normalized RGB for ImGui (0.0-1.0 range)
constexpr float R_NORM = 0.918f; // 234/255
constexpr float G_NORM = 0.627f; // 160/255
constexpr float B_NORM = 0.196f; // 50/255

// ImVec4 for ImGui
ImVec4 GetImGuiColor(float alpha = 1.0f);
} // namespace Primary

// Secondary Brand Color: #32BBED (RGB: 50, 187, 237)
// Blue accent color for secondary elements
namespace Secondary
{
const wxColour WX_COLOR(50, 187, 237);
const std::string HEX = "#32BBED";
const std::string HEX_UPPER = "#32BBED";

// RGB values (0-255 range)
constexpr unsigned char R = 50;
constexpr unsigned char G = 187;
constexpr unsigned char B = 237;

// Normalized RGB for ImGui (0.0-1.0 range)
constexpr float R_NORM = 0.196f; // 50/255
constexpr float G_NORM = 0.733f; // 187/255
constexpr float B_NORM = 0.929f; // 237/255

// ImVec4 for ImGui
ImVec4 GetImGuiColor(float alpha = 1.0f);
} // namespace Secondary

// Darker version of Primary color for button backgrounds, shadows, etc.
// Calculated by reducing lightness by ~15%
namespace PrimaryDark
{
const wxColour WX_COLOR(200, 140, 40);
const std::string HEX = "#C88C28";

// RGB values (0-255 range)
constexpr unsigned char R = 200;
constexpr unsigned char G = 140;
constexpr unsigned char B = 40;

// Normalized RGB for ImGui (0.0-1.0 range)
constexpr float R_NORM = 0.784f; // 200/255
constexpr float G_NORM = 0.549f; // 140/255
constexpr float B_NORM = 0.157f; // 40/255

// ImVec4 for ImGui
ImVec4 GetImGuiColor(float alpha = 1.0f);
} // namespace PrimaryDark

// Complementary color for Primary: #E2BA87 (RGB: 226, 186, 135)
// Lighter tan/beige that complements the orange, used for slice button
namespace Complementary
{
const wxColour WX_COLOR(226, 186, 135);
const std::string HEX = "#E2BA87";
const std::string HEX_UPPER = "#E2BA87";

// RGB values (0-255 range)
constexpr unsigned char R = 226;
constexpr unsigned char G = 186;
constexpr unsigned char B = 135;

// Normalized RGB for ImGui (0.0-1.0 range)
constexpr float R_NORM = 0.886f; // 226/255
constexpr float G_NORM = 0.729f; // 186/255
constexpr float B_NORM = 0.529f; // 135/255

// ImVec4 for ImGui
ImVec4 GetImGuiColor(float alpha = 1.0f);
} // namespace Complementary

// ============================================================================
// Legacy Color Replacements
// ============================================================================
// Legacy colors that should be replaced when found

namespace Legacy
{
const std::string LEGACY_ORANGE = "#ED6B21"; // Legacy orange
const std::string LEGACY_GREEN = "#00AE42";  // Legacy green
} // namespace Legacy

} // namespace Theme
} // namespace GUI
} // namespace Slic3r

#endif // !slic3r_GUI_Theme_hpp_
