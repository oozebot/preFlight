///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include <wx/colour.h>
#include <string>
#include <vector>

// ============================================================================
// ThemePalette - data-driven color palette loaded from a theme JSON file
// ============================================================================
//
// A theme is a flat palette of named color tokens. The default-constructed
// ThemePalette holds preFlight's built-in dark values, so a missing or invalid
// theme file falls back to the exact current appearance (never bricks the UI).
// load_active_theme() overlays any keys present in the on-disk theme file on
// top of those defaults.
//
// The UIColors accessors read their dark tokens from active_dark_palette(), so
// editing the theme file changes the dark appearance without touching code.
// ============================================================================

namespace DSKY
{

// Floating-point RGBA token (0.0-1.0) for ImGui colors that carry an alpha.
struct RGBAf
{
    float r{0.f};
    float g{0.f};
    float b{0.f};
    float a{1.f};
};

struct ThemePalette
{
    // Metadata. The default-constructed palette is the compiled-in "Default Dark" theme.
    std::string name{"Default Dark"};
    bool is_dark{true};

    // --- Input fields ---
    wxColour input_background{22, 27, 34};
    wxColour input_background_disabled{33, 38, 45};
    wxColour input_foreground{201, 209, 217};
    wxColour input_foreground_disabled{110, 118, 129};

    // --- Panels ---
    wxColour panel_background{13, 17, 23};
    wxColour panel_foreground{201, 209, 217};

    // --- Content areas ---
    wxColour content_background{22, 27, 34};
    wxColour content_foreground{201, 209, 217};

    // --- Secondary / badge text ---
    wxColour secondary_text{139, 148, 158};

    // --- Labels / highlight ---
    wxColour label_default{230, 237, 243};
    wxColour highlight_label{201, 209, 217};
    wxColour highlight_background{33, 38, 45};

    // --- Button labels ---
    wxColour hovered_btn_label{253, 111, 40};
    wxColour default_btn_label{255, 181, 100};
    wxColour selected_btn_background{48, 54, 61};

    // --- Headers ---
    wxColour header_background{22, 27, 34};
    wxColour header_hover{33, 38, 45};
    wxColour section_header_background{30, 35, 43};
    wxColour section_header_hover{40, 46, 54};
    wxColour header_divider{48, 54, 61};

    // --- Tab bar ---
    wxColour tab_background_normal{22, 27, 34};
    wxColour tab_background_hover{33, 38, 45};
    wxColour tab_background_selected{48, 54, 61};
    wxColour tab_background_disabled{13, 17, 23};
    wxColour tab_text_normal{139, 148, 158};
    wxColour tab_text_selected{201, 209, 217};
    wxColour tab_text_disabled{72, 79, 88};
    wxColour tab_border{48, 54, 61};

    // --- Borders ---
    wxColour static_box_border{48, 54, 61};
    wxColour section_border{255, 255, 255};

    // --- Canvas / 3D view ---
    wxColour canvas_background{28, 33, 40};
    wxColour canvas_gradient_top{33, 38, 45};
    wxColour bed_surface{48, 54, 61};
    wxColour bed_grid{68, 76, 86};

    // --- Menu bar ---
    wxColour menu_background{22, 27, 34};
    wxColour menu_hover{33, 38, 45};
    wxColour menu_text{201, 209, 217};

    // --- Window title bar ---
    wxColour title_bar_background{13, 17, 23};
    wxColour title_bar_text{201, 209, 217};
    wxColour title_bar_border{48, 54, 61};

    // --- Legend combo box (Preview) ---
    wxColour legend_combo_background{22, 27, 34};
    wxColour legend_combo_background_hovered{33, 38, 45};

    // --- 3D canvas toolbar/gizmo icon colors (monochrome enabled / disabled state variants) ---
    wxColour icon_enabled{255, 255, 255};
    wxColour icon_disabled{128, 128, 128};

    // --- Preview slider / ruler / legend (ImGui float colors with alpha) ---
    RGBAf ruler_background{0.13f, 0.13f, 0.13f, 0.5f};
    RGBAf legend_window_background{0.13f, 0.13f, 0.13f, 0.5f};
    RGBAf slider_groove{0.086f, 0.106f, 0.133f, 0.95f};
    RGBAf slider_border{1.0f, 1.0f, 1.0f, 1.0f};
    RGBAf ruler_tick{1.0f, 1.0f, 1.0f, 1.0f};
    RGBAf slider_label_background{0.188f, 0.212f, 0.239f, 1.0f};
    RGBAf legend_text{1.0f, 1.0f, 1.0f, 1.0f};
    RGBAf gcode_comment{0.7f, 0.7f, 0.7f, 1.0f};
    RGBAf gcode_command{0.8f, 0.8f, 0.0f, 1.0f}; // G/M command tokens in the preview G-code legend

    // --- ImGui window style (ImGuiWrapper::init_style) ---
    RGBAf imgui_window_bg{0.086f, 0.106f, 0.133f, 0.95f};
    RGBAf imgui_frame_bg{0.129f, 0.149f, 0.176f, 1.0f};
    RGBAf imgui_frame_hover{0.188f, 0.212f, 0.239f, 1.0f};
    RGBAf imgui_text{0.788f, 0.820f, 0.851f, 1.0f};
    RGBAf imgui_text_disabled{0.431f, 0.463f, 0.506f, 1.0f};
    RGBAf imgui_border{0.188f, 0.212f, 0.239f, 1.0f};
    RGBAf imgui_header{0.588f, 0.412f, 0.118f, 1.0f};
    RGBAf imgui_header_hover{0.700f, 0.490f, 0.140f, 1.0f};

    // --- 3D canvas toolbar backdrops (main toolbar + gizmo toolbar), with alpha ---
    RGBAf toolbar_background{0.188f, 0.212f, 0.239f, 0.90f};

    // --- Brand / accent ---
    // The interactive accent (focus borders, highlights, links, active indicators), the matching ImGui
    // overlay accent, mode markers, and the themed recolor target for the orange baked into the SVG icons.
    // These default to the preFlight orange, so any theme that omits them keeps the brand accent.
    wxColour accent_primary{234, 160, 50};   // #EAA032 focus/highlight + ImGui COL_ORANGE_LIGHT + icon orange
    wxColour accent_dark{200, 140, 40};      // #C88C28 button fills + ImGui COL_ORANGE_DARK
    wxColour accent_hover{245, 176, 65};     // #F5B041 hover variant
    wxColour accent_secondary{50, 187, 237}; // #32BBED mode markers / secondary brand
    wxColour accent_text{26, 26, 26};        // dark glyph/text drawn on top of an accent fill

    // --- Semantic status colors (error/warning notification backgrounds; reusable for other alert UI) ---
    wxColour error{200, 60, 60};    // #C83C3C
    wxColour warning{224, 168, 48}; // #E0A830 (clean amber, not the old derived brown)
};

// Reserved selection keys. The two "Default" themes are compiled into the binary (not files on
// disk), so they always exist even if the resources/themes folder is emptied. "Auto" follows the
// OS appearance and resolves to one of the two compiled defaults.
inline const char *auto_theme_key()
{
    return "Auto";
}
inline const char *default_dark_key()
{
    return "Default Dark";
}
inline const char *default_light_key()
{
    return "Default Light";
}

// The compiled-in light theme (the dark one is just a default-constructed ThemePalette).
ThemePalette default_light_palette();

// Lightweight descriptor of a theme file on disk (for the Preferences dropdown).
struct ThemeInfo
{
    std::string name;         // display name and selection key (from JSON "name")
    std::string file_path;    // absolute path to the .json
    bool is_dark{true};       // from JSON "is_dark"
    std::string auto_default; // "dark", "light", or empty (Auto pairing role)
};

// Returns the active palette (default-constructed dark values until
// load_active_theme() overlays the resolved on-disk theme file).
const ThemePalette &active_palette();

// Whether the active theme is dark. Drives the binary OS chrome (native
// scrollbars, title bar, tree glyphs) and the app's dark_mode() master.
bool active_theme_is_dark();

// Enumerates theme files in resources/themes (built-in) and data_dir()/themes
// (user-imported). User themes shadow built-ins of the same name.
std::vector<ThemeInfo> available_themes();

// The current theme selection key (app_config "theme"; "Auto" follows the OS),
// falling back to a migration from the legacy dark_color_mode setting, then Auto.
// This is the selection to show in the Preferences dropdown.
std::string current_theme_selection();

// Resolves the configured theme (app_config "theme"; "Auto" follows the OS) and overlays it onto the
// active palette. Safe to call once at startup before any color is read; if the selected theme file is
// missing or invalid, the active palette falls back to the OS appearance (as "Auto" would) and a pending
// error message is recorded for the GUI to surface (see take_theme_load_error()).
void load_active_theme();

// Returns and clears any "selected theme could not be loaded" message recorded by load_active_theme().
// Empty when the theme loaded cleanly. The GUI drains this once the notification system exists.
std::string take_theme_load_error();

} // namespace DSKY

