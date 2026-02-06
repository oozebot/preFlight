///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef slic3r_UI_Colors_hpp_
#define slic3r_UI_Colors_hpp_

#include <wx/colour.h>

// ============================================================================
// Widget UI Colors - Centralized color definitions for preFlight widgets
// ============================================================================
//
// ARCHITECTURE:
// - All colors are defined as Dark/Light pairs (building blocks)
// - Unified accessor functions return the correct color based on current theme
// - Callers should ALWAYS use unified accessors, NEVER check dark_mode() themselves
//
// DARK THEME: GitHub-inspired cool blue-gray palette
//   #0D1117 (13,17,23)   - deepest background (panels, canvas)
//   #161B22 (22,27,34)   - secondary background (inputs, headers)
//   #21262D (33,38,45)   - elevated/hover states
//   #30363D (48,54,61)   - borders, dividers
//   #C9D1D9 (201,209,217) - primary text
//   #8B949E (139,148,158) - secondary text
//   #6E7681 (110,118,129) - muted/disabled text
//
// USAGE:
//   control->SetBackgroundColour(UIColors::InputBackground());  // Correct!
//   control->SetForegroundColour(UIColors::PanelForeground());  // Correct!
//
// NEVER DO THIS:
//   if (dark_mode())
//       control->SetBackgroundColour(UIColors::InputBackgroundDark());
//   else
//       control->SetBackgroundColour(UIColors::InputBackgroundLight());
//
// ============================================================================

// Forward declaration - defined in GUI_App.cpp
// This avoids circular includes while allowing UIColors to check theme state
namespace Slic3r
{
namespace GUI
{
bool IsDarkMode();
}
} // namespace Slic3r

// Legacy static constants (for backward compatibility during transition)
static const int clr_border_normal = 0x30363D;  // GitHub border
static const int clr_border_hovered = 0xEAA032; // preFlight brand orange
static const int clr_border_disabled = 0x30363D;

static const int clr_background_normal_light = 0xFFFDF8;   // Warm white (255,253,248)
static const int clr_background_normal_dark = 0x161B22;    // GitHub #161B22 (22,27,34)
static const int clr_background_focused = 0xEAA032;        // preFlight brand orange
static const int clr_background_disabled_dark = 0x21262D;  // GitHub #21262D (33,38,45)
static const int clr_background_disabled_light = 0xEBE8E4; // Warm light disabled

static const int clr_foreground_normal = 0x262E30;
static const int clr_foreground_focused = 0xEAA032; // preFlight brand orange
static const int clr_foreground_disabled = 0x909090;
static const int clr_foreground_disabled_dark = 0x6E7681; // GitHub muted #6E7681
static const int clr_foreground_disabled_light = 0x909090;

// ============================================================================
// UIColors Namespace - Theme Color API
// ============================================================================

namespace UIColors
{

// ============================================================================
// SECTION 1: Building Blocks - Dark/Light specific colors
// ============================================================================
// These define the actual color values. Use unified accessors below instead.
// ============================================================================

// --- Input Field Colors (Building Blocks) ---

inline wxColour InputBackgroundDark()
{
    return wxColour(22, 27, 34); // GitHub #161B22
}
inline wxColour InputBackgroundLight()
{
    return wxColour(250, 248, 244);
}
inline wxColour InputBackgroundDisabledDark()
{
    return wxColour(33, 38, 45); // GitHub #21262D
}
inline wxColour InputBackgroundDisabledLight()
{
    return wxColour(235, 232, 228);
}

inline wxColour InputForegroundDark()
{
    return wxColour(201, 209, 217); // GitHub #C9D1D9
}
inline wxColour InputForegroundLight()
{
    return wxColour(38, 46, 48);
}
inline wxColour InputForegroundDisabledDark()
{
    return wxColour(110, 118, 129); // GitHub #6E7681
}
inline wxColour InputForegroundDisabledLight()
{
    return wxColour(144, 144, 144);
}

// --- Panel/Background Colors (Building Blocks) ---

inline wxColour PanelBackgroundDark()
{
    return wxColour(13, 17, 23); // GitHub #0D1117
}
inline wxColour PanelBackgroundLight()
{
    return wxColour(250, 248, 244);
}
inline wxColour PanelForegroundDark()
{
    return wxColour(201, 209, 217); // GitHub #C9D1D9
}
inline wxColour PanelForegroundLight()
{
    return wxColour(38, 46, 48);
}

// --- Content Area Colors (Building Blocks) ---
// Content areas (collapsible sections, info panels, static box interiors)
// Use the lighter interior color, not the darkest page background

inline wxColour ContentBackgroundDark()
{
    return wxColour(22, 27, 34); // GitHub #161B22 - same as InputBackgroundDark
}
inline wxColour ContentBackgroundLight()
{
    return wxColour(250, 248, 244);
}
inline wxColour ContentForegroundDark()
{
    return wxColour(201, 209, 217); // GitHub #C9D1D9
}
inline wxColour ContentForegroundLight()
{
    return wxColour(38, 46, 48);
}

// --- Secondary/Badge Text Colors (Building Blocks) ---

inline wxColour SecondaryTextDark()
{
    return wxColour(139, 148, 158); // GitHub #8B949E
}
inline wxColour SecondaryTextLight()
{
    return wxColour(100, 100, 100);
}

// --- Label/Text Colors (Building Blocks) ---

inline wxColour LabelDefaultDark()
{
    return wxColour(230, 237, 243); // GitHub #E6EDF3 (bright text)
}
inline wxColour LabelDefaultLight()
{
    return wxColour(38, 46, 48);
}
inline wxColour HighlightLabelDark()
{
    return wxColour(201, 209, 217); // GitHub #C9D1D9
}
inline wxColour HighlightLabelLight()
{
    return wxColour(60, 60, 60);
}
inline wxColour HighlightBackgroundDark()
{
    return wxColour(33, 38, 45); // GitHub #21262D
}
inline wxColour HighlightBackgroundLight()
{
    return wxColour(220, 215, 210);
}

// --- Button Label Colors (Building Blocks) ---

inline wxColour HoveredBtnLabelDark()
{
    return wxColour(253, 111, 40);
}
inline wxColour HoveredBtnLabelLight()
{
    return wxColour(252, 77, 1);
}
inline wxColour DefaultBtnLabelDark()
{
    return wxColour(255, 181, 100);
}
inline wxColour DefaultBtnLabelLight()
{
    return wxColour(203, 61, 0);
}
inline wxColour SelectedBtnBackgroundDark()
{
    return wxColour(48, 54, 61); // GitHub #30363D
}
inline wxColour SelectedBtnBackgroundLight()
{
    return wxColour(228, 220, 216);
}

// --- Header/List Colors (Building Blocks) ---

inline wxColour HeaderBackgroundDark()
{
    return wxColour(22, 27, 34); // GitHub #161B22
}
inline wxColour HeaderBackgroundLight()
{
    return wxColour(245, 242, 238);
}
inline wxColour HeaderHoverDark()
{
    return wxColour(33, 38, 45); // GitHub #21262D
}
inline wxColour HeaderHoverLight()
{
    return wxColour(235, 228, 218);
}
// Top-level section headers (slightly darker than regular headers for visual hierarchy)
inline wxColour SectionHeaderBackgroundDark()
{
    return wxColour(30, 35, 43);
}
inline wxColour SectionHeaderBackgroundLight()
{
    return wxColour(225, 220, 214);
}
inline wxColour SectionHeaderHoverDark()
{
    return wxColour(40, 46, 54);
}
inline wxColour SectionHeaderHoverLight()
{
    return wxColour(215, 208, 200);
}

inline wxColour HeaderDividerDark()
{
    return wxColour(48, 54, 61); // GitHub #30363D
}
inline wxColour HeaderDividerLight()
{
    return wxColour(200, 195, 190);
}

// --- Tab Bar Colors (Building Blocks) ---

inline wxColour TabBackgroundNormalDark()
{
    return wxColour(22, 27, 34); // GitHub #161B22
}
inline wxColour TabBackgroundNormalLight()
{
    return wxColour(245, 242, 238);
}
inline wxColour TabBackgroundHoverDark()
{
    return wxColour(33, 38, 45); // GitHub #21262D
}
inline wxColour TabBackgroundHoverLight()
{
    return wxColour(238, 232, 225);
}
inline wxColour TabBackgroundSelectedDark()
{
    return wxColour(48, 54, 61); // GitHub #30363D
}
inline wxColour TabBackgroundSelectedLight()
{
    return wxColour(253, 247, 240);
}
inline wxColour TabBackgroundDisabledDark()
{
    return wxColour(13, 17, 23); // GitHub #0D1117
}
inline wxColour TabBackgroundDisabledLight()
{
    return wxColour(200, 197, 193);
}
inline wxColour TabTextNormalDark()
{
    return wxColour(139, 148, 158); // GitHub #8B949E
}
inline wxColour TabTextNormalLight()
{
    return wxColour(102, 102, 102);
}
inline wxColour TabTextSelectedDark()
{
    return wxColour(201, 209, 217); // GitHub #C9D1D9
}
inline wxColour TabTextSelectedLight()
{
    return wxColour(51, 51, 51);
}
inline wxColour TabTextDisabledDark()
{
    return wxColour(72, 79, 88); // GitHub #484F58
}
inline wxColour TabTextDisabledLight()
{
    return wxColour(80, 80, 80);
}
inline wxColour TabBorderDark()
{
    return wxColour(48, 54, 61); // GitHub #30363D
}
inline wxColour TabBorderLight()
{
    return wxColour(224, 218, 212);
}

// --- StaticBox Border Colors (Building Blocks) ---

inline wxColour StaticBoxBorderDark()
{
    return wxColour(48, 54, 61); // GitHub #30363D
}
inline wxColour StaticBoxBorderLight()
{
    return wxColour(180, 175, 165);
}

// --- Accent Colors (Theme-independent) ---

inline wxColour AccentPrimary()
{
    return wxColour(234, 160, 50);
}
inline wxColour AccentHover()
{
    return wxColour(245, 176, 65);
}

// --- Border Colors (Theme-independent) ---

inline wxColour BorderNormal()
{
    return wxColour(48, 54, 61); // GitHub #30363D
}
inline wxColour BorderHovered()
{
    return wxColour(234, 160, 50);
}
inline wxColour BorderDisabled()
{
    return wxColour(48, 54, 61); // GitHub #30363D
}

// ============================================================================
// SECTION 2: Unified Accessors - USE THESE!
// ============================================================================
// These automatically return the correct color for the current theme.
// All widget code should use these functions exclusively.
// ============================================================================

// --- Input Field Colors ---

inline wxColour InputBackground()
{
    return Slic3r::GUI::IsDarkMode() ? InputBackgroundDark() : InputBackgroundLight();
}

inline wxColour InputBackgroundDisabled()
{
    return Slic3r::GUI::IsDarkMode() ? InputBackgroundDisabledDark() : InputBackgroundDisabledLight();
}

inline wxColour InputForeground()
{
    return Slic3r::GUI::IsDarkMode() ? InputForegroundDark() : InputForegroundLight();
}

inline wxColour InputForegroundDisabled()
{
    return Slic3r::GUI::IsDarkMode() ? InputForegroundDisabledDark() : InputForegroundDisabledLight();
}

// --- Panel Colors ---

inline wxColour PanelBackground()
{
    return Slic3r::GUI::IsDarkMode() ? PanelBackgroundDark() : PanelBackgroundLight();
}

inline wxColour PanelForeground()
{
    return Slic3r::GUI::IsDarkMode() ? PanelForegroundDark() : PanelForegroundLight();
}

// --- Content Area Colors ---

inline wxColour ContentBackground()
{
    return Slic3r::GUI::IsDarkMode() ? ContentBackgroundDark() : ContentBackgroundLight();
}

inline wxColour ContentForeground()
{
    return Slic3r::GUI::IsDarkMode() ? ContentForegroundDark() : ContentForegroundLight();
}

// --- Secondary Text ---

inline wxColour SecondaryText()
{
    return Slic3r::GUI::IsDarkMode() ? SecondaryTextDark() : SecondaryTextLight();
}

// --- Labels ---

inline wxColour LabelDefault()
{
    return Slic3r::GUI::IsDarkMode() ? LabelDefaultDark() : LabelDefaultLight();
}

inline wxColour HighlightLabel()
{
    return Slic3r::GUI::IsDarkMode() ? HighlightLabelDark() : HighlightLabelLight();
}

inline wxColour HighlightBackground()
{
    return Slic3r::GUI::IsDarkMode() ? HighlightBackgroundDark() : HighlightBackgroundLight();
}

// --- Button Labels ---

inline wxColour HoveredBtnLabel()
{
    return Slic3r::GUI::IsDarkMode() ? HoveredBtnLabelDark() : HoveredBtnLabelLight();
}

inline wxColour DefaultBtnLabel()
{
    return Slic3r::GUI::IsDarkMode() ? DefaultBtnLabelDark() : DefaultBtnLabelLight();
}

inline wxColour SelectedBtnBackground()
{
    return Slic3r::GUI::IsDarkMode() ? SelectedBtnBackgroundDark() : SelectedBtnBackgroundLight();
}

// --- Headers ---

inline wxColour HeaderBackground()
{
    return Slic3r::GUI::IsDarkMode() ? HeaderBackgroundDark() : HeaderBackgroundLight();
}

inline wxColour HeaderHover()
{
    return Slic3r::GUI::IsDarkMode() ? HeaderHoverDark() : HeaderHoverLight();
}

inline wxColour HeaderDivider()
{
    return Slic3r::GUI::IsDarkMode() ? HeaderDividerDark() : HeaderDividerLight();
}

// --- Tab Bar ---

inline wxColour TabBackgroundNormal()
{
    return Slic3r::GUI::IsDarkMode() ? TabBackgroundNormalDark() : TabBackgroundNormalLight();
}

inline wxColour TabBackgroundHover()
{
    return Slic3r::GUI::IsDarkMode() ? TabBackgroundHoverDark() : TabBackgroundHoverLight();
}

inline wxColour TabBackgroundSelected()
{
    return Slic3r::GUI::IsDarkMode() ? TabBackgroundSelectedDark() : TabBackgroundSelectedLight();
}

inline wxColour TabBackgroundDisabled()
{
    return Slic3r::GUI::IsDarkMode() ? TabBackgroundDisabledDark() : TabBackgroundDisabledLight();
}

inline wxColour TabTextNormal()
{
    return Slic3r::GUI::IsDarkMode() ? TabTextNormalDark() : TabTextNormalLight();
}

inline wxColour TabTextSelected()
{
    return Slic3r::GUI::IsDarkMode() ? TabTextSelectedDark() : TabTextSelectedLight();
}

inline wxColour TabTextDisabled()
{
    return Slic3r::GUI::IsDarkMode() ? TabTextDisabledDark() : TabTextDisabledLight();
}

inline wxColour TabBorder()
{
    return Slic3r::GUI::IsDarkMode() ? TabBorderDark() : TabBorderLight();
}

inline wxColour StaticBoxBorder()
{
    return Slic3r::GUI::IsDarkMode() ? StaticBoxBorderDark() : StaticBoxBorderLight();
}

// ============================================================================
// SECTION 3: Canvas / 3D View Colors
// ============================================================================
// These colors are used for the OpenGL canvas, bed/platter, and grid.
// Convert to ColorRGBA in calling code: ColorRGBA(r/255.0f, g/255.0f, b/255.0f, 1.0f)
// ============================================================================

// --- Canvas Background (area around the bed) ---
// Slightly lighter than toolbar to create contrast

inline wxColour CanvasBackgroundDark()
{
    return wxColour(28, 33, 40); // #1C2128 - lighter than toolbar for contrast
}
inline wxColour CanvasBackgroundLight()
{
    return wxColour(192, 189, 185); // Warm light gray
}

// --- Canvas Gradient Top (lighter part of background gradient) ---
// Slightly lighter still at top for subtle depth

inline wxColour CanvasGradientTopDark()
{
    return wxColour(33, 38, 45); // #21262D - creates gradient from toolbar
}
inline wxColour CanvasGradientTopLight()
{
    return wxColour(200, 197, 193); // Warm light for gradient
}

// --- Bed/Platter Surface ---

inline wxColour BedSurfaceDark()
{
    return wxColour(48, 54, 61); // GitHub #30363D
}
inline wxColour BedSurfaceLight()
{
    return wxColour(180, 177, 173); // Light warm gray bed
}

// --- Bed Grid Lines ---

inline wxColour BedGridDark()
{
    return wxColour(68, 76, 86); // GitHub #444C56
}
inline wxColour BedGridLight()
{
    return wxColour(160, 157, 153); // Subtle grid for light mode
}

// --- Menu Bar Background ---

inline wxColour MenuBackgroundDark()
{
    return wxColour(22, 27, 34); // GitHub #161B22
}
inline wxColour MenuBackgroundLight()
{
    return wxColour(250, 248, 244); // Warm white matching sidebar
}

inline wxColour MenuHoverDark()
{
    return wxColour(33, 38, 45); // GitHub #21262D
}
inline wxColour MenuHoverLight()
{
    return wxColour(235, 232, 228); // Subtle hover
}

inline wxColour MenuTextDark()
{
    return wxColour(201, 209, 217); // GitHub #C9D1D9
}
inline wxColour MenuTextLight()
{
    return wxColour(38, 46, 48); // Match other text
}

// --- Window Title Bar Colors (Windows 11 custom caption) ---
// Title bar is darkest layer to create visual hierarchy with menu/toolbar

inline wxColour TitleBarBackgroundDark()
{
    return wxColour(13, 17, 23); // GitHub #0D1117 - darkest layer
}
inline wxColour TitleBarBackgroundLight()
{
    return wxColour(245, 242, 238); // Slightly darker than content
}
inline wxColour TitleBarTextDark()
{
    return wxColour(201, 209, 217); // GitHub #C9D1D9
}
inline wxColour TitleBarTextLight()
{
    return wxColour(38, 46, 48); // Dark text
}
inline wxColour TitleBarBorderDark()
{
    return wxColour(48, 54, 61); // GitHub #30363D
}
inline wxColour TitleBarBorderLight()
{
    return wxColour(200, 195, 190); // Light border
}

// --- Legend Combo Box Colors (ImGui combo in Preview legend) ---
// These need alpha support, so we provide both RGB values and alpha separately

inline wxColour LegendComboBackgroundDark()
{
    return wxColour(22, 27, 34); // GitHub #161B22 - matches InputBackground
}
inline wxColour LegendComboBackgroundLight()
{
    return wxColour(240, 237, 233); // Light warm gray
}
inline wxColour LegendComboBackgroundHoveredDark()
{
    return wxColour(33, 38, 45); // GitHub #21262D - matches hover states
}
inline wxColour LegendComboBackgroundHoveredLight()
{
    return wxColour(225, 220, 215); // Slightly darker on hover
}

// --- Canvas Unified Accessors ---

inline wxColour CanvasBackground()
{
    return Slic3r::GUI::IsDarkMode() ? CanvasBackgroundDark() : CanvasBackgroundLight();
}

inline wxColour CanvasGradientTop()
{
    return Slic3r::GUI::IsDarkMode() ? CanvasGradientTopDark() : CanvasGradientTopLight();
}

inline wxColour BedSurface()
{
    return Slic3r::GUI::IsDarkMode() ? BedSurfaceDark() : BedSurfaceLight();
}

inline wxColour BedGrid()
{
    return Slic3r::GUI::IsDarkMode() ? BedGridDark() : BedGridLight();
}

inline wxColour MenuBackground()
{
    return Slic3r::GUI::IsDarkMode() ? MenuBackgroundDark() : MenuBackgroundLight();
}

inline wxColour MenuHover()
{
    return Slic3r::GUI::IsDarkMode() ? MenuHoverDark() : MenuHoverLight();
}

inline wxColour MenuText()
{
    return Slic3r::GUI::IsDarkMode() ? MenuTextDark() : MenuTextLight();
}

inline wxColour TitleBarBackground()
{
    return Slic3r::GUI::IsDarkMode() ? TitleBarBackgroundDark() : TitleBarBackgroundLight();
}

inline wxColour TitleBarText()
{
    return Slic3r::GUI::IsDarkMode() ? TitleBarTextDark() : TitleBarTextLight();
}

inline wxColour TitleBarBorder()
{
    return Slic3r::GUI::IsDarkMode() ? TitleBarBorderDark() : TitleBarBorderLight();
}

// --- Legend Combo Box ---

inline wxColour LegendComboBackground()
{
    return Slic3r::GUI::IsDarkMode() ? LegendComboBackgroundDark() : LegendComboBackgroundLight();
}

inline wxColour LegendComboBackgroundHovered()
{
    return Slic3r::GUI::IsDarkMode() ? LegendComboBackgroundHoveredDark() : LegendComboBackgroundHoveredLight();
}

// Alpha value for legend combo (0.0-1.0 for ImGui)
inline float LegendComboAlpha()
{
    return 0.95f;
}

// ============================================================================
// SECTION 4: Preview Slider/Ruler Colors (ImGui)
// ============================================================================
// These are used for the vertical/horizontal layer sliders and rulers in Preview.
// Return raw RGB values for ImGui (0.0-1.0 scale).
// ============================================================================

// --- Preview Ruler Background (semi-transparent overlay) ---

inline void RulerBackgroundRGBA(float &r, float &g, float &b, float &a)
{
    if (Slic3r::GUI::IsDarkMode())
    {
        r = 0.13f;
        g = 0.13f;
        b = 0.13f;
        a = 0.5f;
    }
    else
    {
        // Light mode: warm gray, slightly darker than before
        r = 0.70f;
        g = 0.68f;
        b = 0.66f;
        a = 0.6f;
    }
}

// --- Legend/GCode Window Background ---

inline void LegendWindowBackgroundRGBA(float &r, float &g, float &b, float &a)
{
    if (Slic3r::GUI::IsDarkMode())
    {
        // Dark mode: same as ruler background
        r = 0.13f;
        g = 0.13f;
        b = 0.13f;
        a = 0.5f;
    }
    else
    {
        // Light mode: light cream for good contrast with dark text
        r = 0.95f;
        g = 0.94f;
        b = 0.92f;
        a = 0.9f;
    }
}

// --- Slider Groove Background (the track thumbs slide along) ---

inline void SliderGrooveBackgroundRGBA(float &r, float &g, float &b, float &a)
{
    if (Slic3r::GUI::IsDarkMode())
    {
        // GitHub #161B22 - dark mode groove
        r = 0.086f;
        g = 0.106f;
        b = 0.133f;
        a = 0.95f;
    }
    else
    {
        // Light warm gray groove
        r = 0.70f;
        g = 0.68f;
        b = 0.66f;
        a = 0.95f;
    }
}

// --- Slider Border (outline around the groove) ---

inline void SliderBorderRGBA(float &r, float &g, float &b, float &a)
{
    if (Slic3r::GUI::IsDarkMode())
    {
        // White border for dark mode
        r = 1.0f;
        g = 1.0f;
        b = 1.0f;
        a = 1.0f;
    }
    else
    {
        // Dark gray border for light mode
        r = 0.20f;
        g = 0.20f;
        b = 0.20f;
        a = 1.0f;
    }
}

// --- Ruler Tick Marks (the small lines on the ruler) ---

inline void RulerTickRGBA(float &r, float &g, float &b, float &a)
{
    if (Slic3r::GUI::IsDarkMode())
    {
        // White ticks for dark mode
        r = 1.0f;
        g = 1.0f;
        b = 1.0f;
        a = 1.0f;
    }
    else
    {
        // Dark gray ticks for light mode (matches text)
        r = 0.15f;
        g = 0.18f;
        b = 0.19f;
        a = 1.0f;
    }
}

// --- Slider Label Background (the tooltip-style label showing current value) ---

inline void SliderLabelBackgroundRGBA(float &r, float &g, float &b, float &a)
{
    if (Slic3r::GUI::IsDarkMode())
    {
        // GitHub #30363D - dark mode label background
        r = 0.188f;
        g = 0.212f;
        b = 0.239f;
        a = 1.0f;
    }
    else
    {
        // Light warm gray for light mode
        r = 0.90f;
        g = 0.88f;
        b = 0.86f;
        a = 1.0f;
    }
}

// --- Legend/GCode Text Color (for value text in legends and g-code viewer) ---

inline void LegendTextRGBA(float &r, float &g, float &b, float &a)
{
    if (Slic3r::GUI::IsDarkMode())
    {
        // White text for dark mode
        r = 1.0f;
        g = 1.0f;
        b = 1.0f;
        a = 1.0f;
    }
    else
    {
        // Dark text for light mode
        r = 0.15f;
        g = 0.18f;
        b = 0.19f;
        a = 1.0f;
    }
}

// --- GCode Comment Color (lighter gray for comments) ---

inline void GCodeCommentRGBA(float &r, float &g, float &b, float &a)
{
    if (Slic3r::GUI::IsDarkMode())
    {
        // Light gray for dark mode
        r = 0.7f;
        g = 0.7f;
        b = 0.7f;
        a = 1.0f;
    }
    else
    {
        // Darker gray for light mode
        r = 0.4f;
        g = 0.4f;
        b = 0.4f;
        a = 1.0f;
    }
}

} // namespace UIColors

// ============================================================================
// Windows COLORREF Namespace - For Win32 API code (DarkMode.cpp, etc.)
// ============================================================================

#ifdef _WIN32
#include <windows.h>

namespace UIColorsWin
{

// ============================================================================
// Building Blocks (Dark/Light specific)
// ============================================================================

inline COLORREF InputBackgroundDark()
{
    return RGB(22, 27, 34); // GitHub #161B22
}
inline COLORREF InputBackgroundLight()
{
    return RGB(250, 248, 244);
}
inline COLORREF InputBackgroundDisabledDark()
{
    return RGB(33, 38, 45); // GitHub #21262D
}
inline COLORREF InputBackgroundDisabledLight()
{
    return RGB(235, 232, 228);
}

inline COLORREF TextDark()
{
    return RGB(230, 237, 243); // GitHub #E6EDF3
}
inline COLORREF TextLight()
{
    return RGB(38, 46, 48);
}
inline COLORREF TextDisabledDark()
{
    return RGB(110, 118, 129); // GitHub #6E7681
}
inline COLORREF TextDisabledLight()
{
    return RGB(144, 144, 144);
}

inline COLORREF HeaderBackgroundDark()
{
    return RGB(22, 27, 34); // GitHub #161B22
}
inline COLORREF HeaderBackgroundLight()
{
    return RGB(245, 242, 238);
}
inline COLORREF HeaderDividerDark()
{
    return RGB(48, 54, 61); // GitHub #30363D
}
inline COLORREF HeaderDividerLight()
{
    return RGB(200, 195, 190);
}

inline COLORREF SelectionBorderDark()
{
    return RGB(255, 255, 255);
}
inline COLORREF SelectionBorderLight()
{
    return RGB(0, 0, 0);
}

inline COLORREF SofterBackgroundDark()
{
    return RGB(33, 38, 45); // GitHub #21262D
}
inline COLORREF WindowBackgroundDark()
{
    return RGB(13, 17, 23); // GitHub #0D1117
}
inline COLORREF WindowTextDark()
{
    return RGB(230, 237, 243); // GitHub #E6EDF3
}

inline COLORREF MenuBackgroundDark()
{
    return RGB(22, 27, 34); // GitHub #161B22
}
inline COLORREF MenuBackgroundLight()
{
    return RGB(250, 248, 244);
}
inline COLORREF MenuHotBackgroundDark()
{
    return RGB(33, 38, 45); // GitHub #21262D
}
inline COLORREF MenuHotBackgroundLight()
{
    return RGB(235, 232, 228);
}
inline COLORREF MenuTextDark()
{
    return RGB(201, 209, 217); // GitHub #C9D1D9
}
inline COLORREF MenuTextLight()
{
    return RGB(38, 46, 48);
}
inline COLORREF MenuDisabledTextDark()
{
    return RGB(110, 118, 129); // GitHub #6E7681
}
inline COLORREF MenuDisabledTextLight()
{
    return RGB(144, 144, 144);
}

inline COLORREF StaticBoxBorderDark()
{
    return RGB(48, 54, 61); // GitHub #30363D
}
inline COLORREF StaticBoxBorderLight()
{
    return RGB(180, 175, 165);
}

// --- Window Title Bar Colors (Windows 11 custom caption) ---
// Title bar is darkest layer to create visual hierarchy with menu/toolbar

inline COLORREF TitleBarBackgroundDark()
{
    return RGB(13, 17, 23); // GitHub #0D1117 - darkest layer
}
inline COLORREF TitleBarBackgroundLight()
{
    return RGB(245, 242, 238); // Slightly darker than content
}
inline COLORREF TitleBarTextDark()
{
    return RGB(201, 209, 217); // GitHub #C9D1D9
}
inline COLORREF TitleBarTextLight()
{
    return RGB(38, 46, 48); // Dark text
}
inline COLORREF TitleBarBorderDark()
{
    return RGB(48, 54, 61); // GitHub #30363D
}
inline COLORREF TitleBarBorderLight()
{
    return RGB(200, 195, 190); // Light border
}

// ============================================================================
// Unified Accessors
// ============================================================================

inline COLORREF InputBackground()
{
    return Slic3r::GUI::IsDarkMode() ? InputBackgroundDark() : InputBackgroundLight();
}

inline COLORREF InputBackgroundDisabled()
{
    return Slic3r::GUI::IsDarkMode() ? InputBackgroundDisabledDark() : InputBackgroundDisabledLight();
}

inline COLORREF Text()
{
    return Slic3r::GUI::IsDarkMode() ? TextDark() : TextLight();
}

inline COLORREF TextDisabled()
{
    return Slic3r::GUI::IsDarkMode() ? TextDisabledDark() : TextDisabledLight();
}

inline COLORREF HeaderBackground()
{
    return Slic3r::GUI::IsDarkMode() ? HeaderBackgroundDark() : HeaderBackgroundLight();
}

inline COLORREF HeaderDivider()
{
    return Slic3r::GUI::IsDarkMode() ? HeaderDividerDark() : HeaderDividerLight();
}

inline COLORREF SelectionBorder()
{
    return Slic3r::GUI::IsDarkMode() ? SelectionBorderDark() : SelectionBorderLight();
}

inline COLORREF StaticBoxBorder()
{
    return Slic3r::GUI::IsDarkMode() ? StaticBoxBorderDark() : StaticBoxBorderLight();
}

inline COLORREF MenuBackground()
{
    return Slic3r::GUI::IsDarkMode() ? MenuBackgroundDark() : MenuBackgroundLight();
}

inline COLORREF MenuHotBackground()
{
    return Slic3r::GUI::IsDarkMode() ? MenuHotBackgroundDark() : MenuHotBackgroundLight();
}

inline COLORREF MenuText()
{
    return Slic3r::GUI::IsDarkMode() ? MenuTextDark() : MenuTextLight();
}

inline COLORREF MenuDisabledText()
{
    return Slic3r::GUI::IsDarkMode() ? MenuDisabledTextDark() : MenuDisabledTextLight();
}

inline COLORREF TitleBarBackground()
{
    return Slic3r::GUI::IsDarkMode() ? TitleBarBackgroundDark() : TitleBarBackgroundLight();
}

inline COLORREF TitleBarText()
{
    return Slic3r::GUI::IsDarkMode() ? TitleBarTextDark() : TitleBarTextLight();
}

inline COLORREF TitleBarBorder()
{
    return Slic3r::GUI::IsDarkMode() ? TitleBarBorderDark() : TitleBarBorderLight();
}

} // namespace UIColorsWin

#endif // _WIN32

#endif // !slic3r_UI_Colors_hpp_
