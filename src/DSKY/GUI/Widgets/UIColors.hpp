///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <wx/colour.h>

#include "../ThemePalette.hpp" // Data-driven dark palette (active_palette())

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
namespace DSKY
{

bool IsDarkMode();
} // namespace DSKY

// Pack a wxColour into a 0xRRGGBB int for StateColor (which takes packed ints).
inline int wxcolour_to_rgb_int(const wxColour &c)
{
    return (c.Red() << 16) | (c.Green() << 8) | c.Blue();
}
inline int accent_primary_rgb()
{
    return wxcolour_to_rgb_int(DSKY::active_palette().accent_primary);
}
inline int accent_hover_rgb()
{
    return wxcolour_to_rgb_int(DSKY::active_palette().accent_hover);
}

// Legacy static constants (for backward compatibility during transition)
static const int clr_border_normal = 0x30363D; // GitHub border
inline int clr_border_hovered()
{
    return accent_primary_rgb();
} // themed accent
static const int clr_border_disabled = 0x30363D;

static const int clr_background_normal_light = 0xFFFDF8; // Warm white (255,253,248)
static const int clr_background_normal_dark = 0x161B22;  // GitHub #161B22 (22,27,34)
inline int clr_background_focused()
{
    return accent_primary_rgb();
} // themed accent
static const int clr_background_disabled_dark = 0x21262D;  // GitHub #21262D (33,38,45)
static const int clr_background_disabled_light = 0xEBE8E4; // Warm light disabled

static const int clr_foreground_normal = 0x262E30;
inline int clr_foreground_focused()
{
    return accent_primary_rgb();
} // themed accent
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
    return DSKY::active_palette().input_background;
}
inline wxColour InputBackgroundLight()
{
    return DSKY::active_palette().input_background;
}
inline wxColour InputBackgroundDisabledDark()
{
    return DSKY::active_palette().input_background_disabled;
}
inline wxColour InputBackgroundDisabledLight()
{
    return DSKY::active_palette().input_background_disabled;
}

inline wxColour InputForegroundDark()
{
    return DSKY::active_palette().input_foreground;
}
inline wxColour InputForegroundLight()
{
    return DSKY::active_palette().input_foreground;
}
inline wxColour InputForegroundDisabledDark()
{
    return DSKY::active_palette().input_foreground_disabled;
}
inline wxColour InputForegroundDisabledLight()
{
    return DSKY::active_palette().input_foreground_disabled;
}

// --- Panel/Background Colors (Building Blocks) ---

inline wxColour PanelBackgroundDark()
{
    return DSKY::active_palette().panel_background;
}
inline wxColour PanelBackgroundLight()
{
    return DSKY::active_palette().panel_background;
}
inline wxColour PanelForegroundDark()
{
    return DSKY::active_palette().panel_foreground;
}
inline wxColour PanelForegroundLight()
{
    return DSKY::active_palette().panel_foreground;
}

// --- Content Area Colors (Building Blocks) ---
// Content areas (collapsible sections, info panels, static box interiors)
// Use the lighter interior color, not the darkest page background

inline wxColour ContentBackgroundDark()
{
    return DSKY::active_palette().content_background;
}
inline wxColour ContentBackgroundLight()
{
    return DSKY::active_palette().content_background;
}
inline wxColour ContentForegroundDark()
{
    return DSKY::active_palette().content_foreground;
}
inline wxColour ContentForegroundLight()
{
    return DSKY::active_palette().content_foreground;
}

// --- Secondary/Badge Text Colors (Building Blocks) ---

inline wxColour SecondaryTextDark()
{
    return DSKY::active_palette().secondary_text;
}
inline wxColour SecondaryTextLight()
{
    return DSKY::active_palette().secondary_text;
}

// --- Label/Text Colors (Building Blocks) ---

inline wxColour LabelDefaultDark()
{
    return DSKY::active_palette().label_default;
}
inline wxColour LabelDefaultLight()
{
    return DSKY::active_palette().label_default;
}
inline wxColour HighlightLabelDark()
{
    return DSKY::active_palette().highlight_label;
}
inline wxColour HighlightLabelLight()
{
    return DSKY::active_palette().highlight_label;
}
inline wxColour HighlightBackgroundDark()
{
    return DSKY::active_palette().highlight_background;
}
inline wxColour HighlightBackgroundLight()
{
    return DSKY::active_palette().highlight_background;
}

// --- Button Label Colors (Building Blocks) ---

inline wxColour HoveredBtnLabelDark()
{
    return DSKY::active_palette().hovered_btn_label;
}
inline wxColour HoveredBtnLabelLight()
{
    return DSKY::active_palette().hovered_btn_label;
}
inline wxColour DefaultBtnLabelDark()
{
    return DSKY::active_palette().default_btn_label;
}
inline wxColour DefaultBtnLabelLight()
{
    return DSKY::active_palette().default_btn_label;
}
inline wxColour SelectedBtnBackgroundDark()
{
    return DSKY::active_palette().selected_btn_background;
}
inline wxColour SelectedBtnBackgroundLight()
{
    return DSKY::active_palette().selected_btn_background;
}

// --- Header/List Colors (Building Blocks) ---

inline wxColour HeaderBackgroundDark()
{
    return DSKY::active_palette().header_background;
}
inline wxColour HeaderBackgroundLight()
{
    return DSKY::active_palette().header_background;
}
inline wxColour HeaderHoverDark()
{
    return DSKY::active_palette().header_hover;
}
inline wxColour HeaderHoverLight()
{
    return DSKY::active_palette().header_hover;
}
// Top-level section headers (slightly darker than regular headers for visual hierarchy)
inline wxColour SectionHeaderBackgroundDark()
{
    return DSKY::active_palette().section_header_background;
}
inline wxColour SectionHeaderBackgroundLight()
{
    return DSKY::active_palette().section_header_background;
}
inline wxColour SectionHeaderHoverDark()
{
    return DSKY::active_palette().section_header_hover;
}
inline wxColour SectionHeaderHoverLight()
{
    return DSKY::active_palette().section_header_hover;
}

inline wxColour HeaderDividerDark()
{
    return DSKY::active_palette().header_divider;
}
inline wxColour HeaderDividerLight()
{
    return DSKY::active_palette().header_divider;
}

// --- Tab Bar Colors (Building Blocks) ---

inline wxColour TabBackgroundNormalDark()
{
    return DSKY::active_palette().tab_background_normal;
}
inline wxColour TabBackgroundNormalLight()
{
    return DSKY::active_palette().tab_background_normal;
}
inline wxColour TabBackgroundHoverDark()
{
    return DSKY::active_palette().tab_background_hover;
}
inline wxColour TabBackgroundHoverLight()
{
    return DSKY::active_palette().tab_background_hover;
}
inline wxColour TabBackgroundSelectedDark()
{
    return DSKY::active_palette().tab_background_selected;
}
inline wxColour TabBackgroundSelectedLight()
{
    return DSKY::active_palette().tab_background_selected;
}
inline wxColour TabBackgroundDisabledDark()
{
    return DSKY::active_palette().tab_background_disabled;
}
inline wxColour TabBackgroundDisabledLight()
{
    return DSKY::active_palette().tab_background_disabled;
}
inline wxColour TabTextNormalDark()
{
    return DSKY::active_palette().tab_text_normal;
}
inline wxColour TabTextNormalLight()
{
    return DSKY::active_palette().tab_text_normal;
}
inline wxColour TabTextSelectedDark()
{
    return DSKY::active_palette().tab_text_selected;
}
inline wxColour TabTextSelectedLight()
{
    return DSKY::active_palette().tab_text_selected;
}
inline wxColour TabTextDisabledDark()
{
    return DSKY::active_palette().tab_text_disabled;
}
inline wxColour TabTextDisabledLight()
{
    return DSKY::active_palette().tab_text_disabled;
}
inline wxColour TabBorderDark()
{
    return DSKY::active_palette().tab_border;
}
inline wxColour TabBorderLight()
{
    return DSKY::active_palette().tab_border;
}

// --- StaticBox Border Colors (Building Blocks) ---

inline wxColour StaticBoxBorderDark()
{
    return DSKY::active_palette().static_box_border;
}
inline wxColour StaticBoxBorderLight()
{
    return DSKY::active_palette().static_box_border;
}

// --- FlatStaticBox (Section Group) Border Colors ---

inline wxColour SectionBorderDark()
{
    return DSKY::active_palette().section_border;
}
inline wxColour SectionBorderLight()
{
    return DSKY::active_palette().section_border;
}

// --- Accent Colors (themed; default to preFlight orange when a theme omits them) ---

inline wxColour AccentPrimary()
{
    return DSKY::active_palette().accent_primary;
}
inline wxColour AccentHover()
{
    return DSKY::active_palette().accent_hover;
}
inline wxColour AccentDark()
{
    return DSKY::active_palette().accent_dark;
}
inline wxColour AccentSecondary()
{
    return DSKY::active_palette().accent_secondary;
}
inline wxColour AccentText()
{
    return DSKY::active_palette().accent_text;
}

// --- Semantic status colors (themed) ---

inline wxColour Error()
{
    return DSKY::active_palette().error;
}
inline wxColour Warning()
{
    return DSKY::active_palette().warning;
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
    return DSKY::IsDarkMode() ? InputBackgroundDark() : InputBackgroundLight();
}

inline wxColour InputBackgroundDisabled()
{
    return DSKY::IsDarkMode() ? InputBackgroundDisabledDark() : InputBackgroundDisabledLight();
}

inline wxColour InputForeground()
{
    return DSKY::IsDarkMode() ? InputForegroundDark() : InputForegroundLight();
}

inline wxColour InputForegroundDisabled()
{
    return DSKY::IsDarkMode() ? InputForegroundDisabledDark() : InputForegroundDisabledLight();
}

// --- Panel Colors ---

inline wxColour PanelBackground()
{
    return DSKY::IsDarkMode() ? PanelBackgroundDark() : PanelBackgroundLight();
}

inline wxColour PanelForeground()
{
    return DSKY::IsDarkMode() ? PanelForegroundDark() : PanelForegroundLight();
}

// --- Content Area Colors ---

inline wxColour ContentBackground()
{
    return DSKY::IsDarkMode() ? ContentBackgroundDark() : ContentBackgroundLight();
}

inline wxColour ContentForeground()
{
    return DSKY::IsDarkMode() ? ContentForegroundDark() : ContentForegroundLight();
}

// --- Secondary Text ---

inline wxColour SecondaryText()
{
    return DSKY::IsDarkMode() ? SecondaryTextDark() : SecondaryTextLight();
}

// --- Labels ---

inline wxColour LabelDefault()
{
    return DSKY::IsDarkMode() ? LabelDefaultDark() : LabelDefaultLight();
}

inline wxColour HighlightLabel()
{
    return DSKY::IsDarkMode() ? HighlightLabelDark() : HighlightLabelLight();
}

inline wxColour HighlightBackground()
{
    return DSKY::IsDarkMode() ? HighlightBackgroundDark() : HighlightBackgroundLight();
}

// --- Button Labels ---

inline wxColour HoveredBtnLabel()
{
    return DSKY::IsDarkMode() ? HoveredBtnLabelDark() : HoveredBtnLabelLight();
}

inline wxColour DefaultBtnLabel()
{
    return DSKY::IsDarkMode() ? DefaultBtnLabelDark() : DefaultBtnLabelLight();
}

inline wxColour SelectedBtnBackground()
{
    return DSKY::IsDarkMode() ? SelectedBtnBackgroundDark() : SelectedBtnBackgroundLight();
}

// --- Headers ---

inline wxColour HeaderBackground()
{
    return DSKY::IsDarkMode() ? HeaderBackgroundDark() : HeaderBackgroundLight();
}

inline wxColour HeaderHover()
{
    return DSKY::IsDarkMode() ? HeaderHoverDark() : HeaderHoverLight();
}

inline wxColour HeaderDivider()
{
    return DSKY::IsDarkMode() ? HeaderDividerDark() : HeaderDividerLight();
}

// --- Tab Bar ---

inline wxColour TabBackgroundNormal()
{
    return DSKY::IsDarkMode() ? TabBackgroundNormalDark() : TabBackgroundNormalLight();
}

inline wxColour TabBackgroundHover()
{
    return DSKY::IsDarkMode() ? TabBackgroundHoverDark() : TabBackgroundHoverLight();
}

inline wxColour TabBackgroundSelected()
{
    return DSKY::IsDarkMode() ? TabBackgroundSelectedDark() : TabBackgroundSelectedLight();
}

inline wxColour TabBackgroundDisabled()
{
    return DSKY::IsDarkMode() ? TabBackgroundDisabledDark() : TabBackgroundDisabledLight();
}

inline wxColour TabTextNormal()
{
    return DSKY::IsDarkMode() ? TabTextNormalDark() : TabTextNormalLight();
}

inline wxColour TabTextSelected()
{
    return DSKY::IsDarkMode() ? TabTextSelectedDark() : TabTextSelectedLight();
}

inline wxColour TabTextDisabled()
{
    return DSKY::IsDarkMode() ? TabTextDisabledDark() : TabTextDisabledLight();
}

inline wxColour TabBorder()
{
    return DSKY::IsDarkMode() ? TabBorderDark() : TabBorderLight();
}

inline wxColour StaticBoxBorder()
{
    return DSKY::IsDarkMode() ? StaticBoxBorderDark() : StaticBoxBorderLight();
}

inline wxColour SectionBorder()
{
    return DSKY::IsDarkMode() ? SectionBorderDark() : SectionBorderLight();
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
    return DSKY::active_palette().canvas_background;
}
inline wxColour CanvasBackgroundLight()
{
    return DSKY::active_palette().canvas_background;
}

// --- Canvas Gradient Top (lighter part of background gradient) ---
// Slightly lighter still at top for subtle depth

inline wxColour CanvasGradientTopDark()
{
    return DSKY::active_palette().canvas_gradient_top;
}
inline wxColour CanvasGradientTopLight()
{
    return DSKY::active_palette().canvas_gradient_top;
}

// --- Bed/Platter Surface ---

inline wxColour BedSurfaceDark()
{
    return DSKY::active_palette().bed_surface;
}
inline wxColour BedSurfaceLight()
{
    return DSKY::active_palette().bed_surface;
}

// --- Bed Grid Lines ---

inline wxColour BedGridDark()
{
    return DSKY::active_palette().bed_grid;
}
inline wxColour BedGridLight()
{
    return DSKY::active_palette().bed_grid;
}

// --- Menu Bar Background ---

inline wxColour MenuBackgroundDark()
{
    return DSKY::active_palette().menu_background;
}
inline wxColour MenuBackgroundLight()
{
    return DSKY::active_palette().menu_background;
}

inline wxColour MenuHoverDark()
{
    return DSKY::active_palette().menu_hover;
}
inline wxColour MenuHoverLight()
{
    return DSKY::active_palette().menu_hover;
}

inline wxColour MenuTextDark()
{
    return DSKY::active_palette().menu_text;
}
inline wxColour MenuTextLight()
{
    return DSKY::active_palette().menu_text;
}

// --- Window Title Bar Colors (Windows 11 custom caption) ---
// Title bar is darkest layer to create visual hierarchy with menu/toolbar

inline wxColour TitleBarBackgroundDark()
{
    return DSKY::active_palette().title_bar_background;
}
inline wxColour TitleBarBackgroundLight()
{
    return DSKY::active_palette().title_bar_background;
}
inline wxColour TitleBarTextDark()
{
    return DSKY::active_palette().title_bar_text;
}
inline wxColour TitleBarTextLight()
{
    return DSKY::active_palette().title_bar_text;
}
inline wxColour TitleBarBorderDark()
{
    return DSKY::active_palette().title_bar_border;
}
inline wxColour TitleBarBorderLight()
{
    return DSKY::active_palette().title_bar_border;
}

// --- Legend Combo Box Colors (ImGui combo in Preview legend) ---
// These need alpha support, so we provide both RGB values and alpha separately

inline wxColour LegendComboBackgroundDark()
{
    return DSKY::active_palette().legend_combo_background;
}
inline wxColour LegendComboBackgroundLight()
{
    return DSKY::active_palette().legend_combo_background;
}
inline wxColour LegendComboBackgroundHoveredDark()
{
    return DSKY::active_palette().legend_combo_background_hovered;
}
inline wxColour LegendComboBackgroundHoveredLight()
{
    return DSKY::active_palette().legend_combo_background_hovered;
}

// --- Canvas Unified Accessors ---

inline wxColour CanvasBackground()
{
    return DSKY::IsDarkMode() ? CanvasBackgroundDark() : CanvasBackgroundLight();
}

inline wxColour CanvasGradientTop()
{
    return DSKY::IsDarkMode() ? CanvasGradientTopDark() : CanvasGradientTopLight();
}

inline wxColour BedSurface()
{
    return DSKY::IsDarkMode() ? BedSurfaceDark() : BedSurfaceLight();
}

inline wxColour BedGrid()
{
    return DSKY::IsDarkMode() ? BedGridDark() : BedGridLight();
}

inline wxColour MenuBackground()
{
    return DSKY::IsDarkMode() ? MenuBackgroundDark() : MenuBackgroundLight();
}

inline wxColour MenuHover()
{
    return DSKY::IsDarkMode() ? MenuHoverDark() : MenuHoverLight();
}

inline wxColour MenuText()
{
    return DSKY::IsDarkMode() ? MenuTextDark() : MenuTextLight();
}

inline wxColour TitleBarBackground()
{
    return DSKY::IsDarkMode() ? TitleBarBackgroundDark() : TitleBarBackgroundLight();
}

inline wxColour TitleBarText()
{
    return DSKY::IsDarkMode() ? TitleBarTextDark() : TitleBarTextLight();
}

inline wxColour TitleBarBorder()
{
    return DSKY::IsDarkMode() ? TitleBarBorderDark() : TitleBarBorderLight();
}

// --- Legend Combo Box ---

inline wxColour LegendComboBackground()
{
    return DSKY::IsDarkMode() ? LegendComboBackgroundDark() : LegendComboBackgroundLight();
}

inline wxColour LegendComboBackgroundHovered()
{
    return DSKY::IsDarkMode() ? LegendComboBackgroundHoveredDark() : LegendComboBackgroundHoveredLight();
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
    const DSKY::RGBAf &c = DSKY::active_palette().ruler_background;
    r = c.r;
    g = c.g;
    b = c.b;
    a = c.a;
}

// --- Legend/GCode Window Background ---

inline void LegendWindowBackgroundRGBA(float &r, float &g, float &b, float &a)
{
    const DSKY::RGBAf &c = DSKY::active_palette().legend_window_background;
    r = c.r;
    g = c.g;
    b = c.b;
    a = c.a;
}

// --- Slider Groove Background (the track thumbs slide along) ---

inline void SliderGrooveBackgroundRGBA(float &r, float &g, float &b, float &a)
{
    const DSKY::RGBAf &c = DSKY::active_palette().slider_groove;
    r = c.r;
    g = c.g;
    b = c.b;
    a = c.a;
}

// --- Slider Border (outline around the groove) ---

inline void SliderBorderRGBA(float &r, float &g, float &b, float &a)
{
    const DSKY::RGBAf &c = DSKY::active_palette().slider_border;
    r = c.r;
    g = c.g;
    b = c.b;
    a = c.a;
}

// --- Ruler Tick Marks (the small lines on the ruler) ---

inline void RulerTickRGBA(float &r, float &g, float &b, float &a)
{
    const DSKY::RGBAf &c = DSKY::active_palette().ruler_tick;
    r = c.r;
    g = c.g;
    b = c.b;
    a = c.a;
}

// --- Slider Label Background (the tooltip-style label showing current value) ---

inline void SliderLabelBackgroundRGBA(float &r, float &g, float &b, float &a)
{
    const DSKY::RGBAf &c = DSKY::active_palette().slider_label_background;
    r = c.r;
    g = c.g;
    b = c.b;
    a = c.a;
}

// --- Legend/GCode Text Color (for value text in legends and g-code viewer) ---

inline void LegendTextRGBA(float &r, float &g, float &b, float &a)
{
    const DSKY::RGBAf &c = DSKY::active_palette().legend_text;
    r = c.r;
    g = c.g;
    b = c.b;
    a = c.a;
}

// --- GCode Comment Color (lighter gray for comments) ---

inline void GCodeCommentRGBA(float &r, float &g, float &b, float &a)
{
    const DSKY::RGBAf &c = DSKY::active_palette().gcode_comment;
    r = c.r;
    g = c.g;
    b = c.b;
    a = c.a;
}

// --- GCode Command Color (G/M command tokens in the preview legend) ---

inline void GCodeCommandRGBA(float &r, float &g, float &b, float &a)
{
    const DSKY::RGBAf &c = DSKY::active_palette().gcode_command;
    r = c.r;
    g = c.g;
    b = c.b;
    a = c.a;
}

} // namespace UIColors

// ============================================================================
// Windows COLORREF Namespace - For Win32 API code (DarkMode.cpp, etc.)
// ============================================================================

#ifdef _WIN32
#include <wx/msw/wrapwin.h>

namespace UIColorsWin
{

// Convert a palette wxColour token to a Win32 COLORREF.
inline COLORREF to_colorref(const wxColour &c)
{
    return RGB(c.Red(), c.Green(), c.Blue());
}

// ============================================================================
// Building Blocks (Dark/Light specific)
// ============================================================================

inline COLORREF InputBackgroundDark()
{
    return to_colorref(DSKY::active_palette().input_background);
}
inline COLORREF InputBackgroundLight()
{
    return to_colorref(DSKY::active_palette().input_background);
}
inline COLORREF InputBackgroundDisabledDark()
{
    return to_colorref(DSKY::active_palette().input_background_disabled);
}
inline COLORREF InputBackgroundDisabledLight()
{
    return to_colorref(DSKY::active_palette().input_background_disabled);
}

inline COLORREF TextDark()
{
    return to_colorref(DSKY::active_palette().label_default);
}
inline COLORREF TextLight()
{
    return to_colorref(DSKY::active_palette().label_default);
}
inline COLORREF TextDisabledDark()
{
    return to_colorref(DSKY::active_palette().input_foreground_disabled);
}
inline COLORREF TextDisabledLight()
{
    return to_colorref(DSKY::active_palette().input_foreground_disabled);
}

inline COLORREF HeaderBackgroundDark()
{
    return to_colorref(DSKY::active_palette().header_background);
}
inline COLORREF HeaderBackgroundLight()
{
    return to_colorref(DSKY::active_palette().header_background);
}
inline COLORREF HeaderDividerDark()
{
    return to_colorref(DSKY::active_palette().header_divider);
}
inline COLORREF HeaderDividerLight()
{
    return to_colorref(DSKY::active_palette().header_divider);
}

inline COLORREF SelectionBorderDark()
{
    return to_colorref(DSKY::active_palette().section_border);
}
inline COLORREF SelectionBorderLight()
{
    return to_colorref(DSKY::active_palette().section_border);
}

inline COLORREF AccentPrimary()
{
    return to_colorref(DSKY::active_palette().accent_primary);
}

inline COLORREF SofterBackgroundDark()
{
    return to_colorref(DSKY::active_palette().highlight_background);
}
inline COLORREF WindowBackgroundDark()
{
    return to_colorref(DSKY::active_palette().panel_background);
}
inline COLORREF WindowTextDark()
{
    return to_colorref(DSKY::active_palette().label_default);
}

inline COLORREF MenuBackgroundDark()
{
    return to_colorref(DSKY::active_palette().menu_background);
}
inline COLORREF MenuBackgroundLight()
{
    return to_colorref(DSKY::active_palette().menu_background);
}
inline COLORREF MenuHotBackgroundDark()
{
    return to_colorref(DSKY::active_palette().menu_hover);
}
inline COLORREF MenuHotBackgroundLight()
{
    return to_colorref(DSKY::active_palette().menu_hover);
}
inline COLORREF MenuTextDark()
{
    return to_colorref(DSKY::active_palette().menu_text);
}
inline COLORREF MenuTextLight()
{
    return to_colorref(DSKY::active_palette().menu_text);
}
inline COLORREF MenuDisabledTextDark()
{
    return to_colorref(DSKY::active_palette().input_foreground_disabled);
}
inline COLORREF MenuDisabledTextLight()
{
    return to_colorref(DSKY::active_palette().input_foreground_disabled);
}

inline COLORREF StaticBoxBorderDark()
{
    return to_colorref(DSKY::active_palette().static_box_border);
}
inline COLORREF StaticBoxBorderLight()
{
    return to_colorref(DSKY::active_palette().static_box_border);
}

// --- Window Title Bar Colors (Windows 11 custom caption) ---
// Title bar is darkest layer to create visual hierarchy with menu/toolbar

inline COLORREF TitleBarBackgroundDark()
{
    return to_colorref(DSKY::active_palette().title_bar_background);
}
inline COLORREF TitleBarBackgroundLight()
{
    return to_colorref(DSKY::active_palette().title_bar_background);
}
inline COLORREF TitleBarTextDark()
{
    return to_colorref(DSKY::active_palette().title_bar_text);
}
inline COLORREF TitleBarTextLight()
{
    return to_colorref(DSKY::active_palette().title_bar_text);
}
inline COLORREF TitleBarBorderDark()
{
    return to_colorref(DSKY::active_palette().title_bar_border);
}
inline COLORREF TitleBarBorderLight()
{
    return to_colorref(DSKY::active_palette().title_bar_border);
}

// ============================================================================
// Unified Accessors
// ============================================================================

inline COLORREF InputBackground()
{
    return DSKY::IsDarkMode() ? InputBackgroundDark() : InputBackgroundLight();
}

inline COLORREF InputBackgroundDisabled()
{
    return DSKY::IsDarkMode() ? InputBackgroundDisabledDark() : InputBackgroundDisabledLight();
}

inline COLORREF Text()
{
    return DSKY::IsDarkMode() ? TextDark() : TextLight();
}

inline COLORREF TextDisabled()
{
    return DSKY::IsDarkMode() ? TextDisabledDark() : TextDisabledLight();
}

inline COLORREF HeaderBackground()
{
    return DSKY::IsDarkMode() ? HeaderBackgroundDark() : HeaderBackgroundLight();
}

inline COLORREF HeaderDivider()
{
    return DSKY::IsDarkMode() ? HeaderDividerDark() : HeaderDividerLight();
}

inline COLORREF SelectionBorder()
{
    return DSKY::IsDarkMode() ? SelectionBorderDark() : SelectionBorderLight();
}

inline COLORREF StaticBoxBorder()
{
    return DSKY::IsDarkMode() ? StaticBoxBorderDark() : StaticBoxBorderLight();
}

inline COLORREF MenuBackground()
{
    return DSKY::IsDarkMode() ? MenuBackgroundDark() : MenuBackgroundLight();
}

inline COLORREF MenuHotBackground()
{
    return DSKY::IsDarkMode() ? MenuHotBackgroundDark() : MenuHotBackgroundLight();
}

inline COLORREF MenuText()
{
    return DSKY::IsDarkMode() ? MenuTextDark() : MenuTextLight();
}

inline COLORREF MenuDisabledText()
{
    return DSKY::IsDarkMode() ? MenuDisabledTextDark() : MenuDisabledTextLight();
}

inline COLORREF TitleBarBackground()
{
    return DSKY::IsDarkMode() ? TitleBarBackgroundDark() : TitleBarBackgroundLight();
}

inline COLORREF TitleBarText()
{
    return DSKY::IsDarkMode() ? TitleBarTextDark() : TitleBarTextLight();
}

inline COLORREF TitleBarBorder()
{
    return DSKY::IsDarkMode() ? TitleBarBorderDark() : TitleBarBorderLight();
}

} // namespace UIColorsWin

#endif // _WIN32
