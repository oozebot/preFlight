///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2019 Vojtěch Bubník @bubnikv, Vojtěch Král @vojtechkral
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <wx/event.h>

namespace DSKY
{

#if __APPLE__
extern bool mac_dark_mode();
// Force the whole application's appearance (NSAppearance) to match the active preFlight theme's
// light/dark, so native controls (buttons, static text, etc.) render in the right mode regardless
// of the macOS system setting.
extern void mac_set_appearance(bool dark);
extern double mac_max_scaling_factor();
// Disable horizontal scrollbar on a wxWindow backed by NSScrollView (e.g. wxDataViewCtrl)
extern void mac_disable_horizontal_scroll(void *nsview_handle);
// Returns the width available for the Name (first) column in an NSOutlineView,
// after accounting for all other columns, intercell spacing, and outline indentation.
// Returns -1 if the view cannot be found.
extern int mac_get_outlineview_name_width(void *nsview_handle);
// Configure a wxStaticBox (NSBox) as NSBoxCustom so we can control its colors.
// The default NSBoxPrimary ignores SetBackgroundColour() and uses system dark mode colors.
extern void mac_set_staticbox_transparent(void *nsview_handle);
// Set fill, border, and title colors on an NSBoxCustom-configured static box.
extern void mac_set_staticbox_colors(void *nsview_handle, unsigned char fill_r, unsigned char fill_g,
                                     unsigned char fill_b, unsigned char border_r, unsigned char border_g,
                                     unsigned char border_b, unsigned char title_r, unsigned char title_g,
                                     unsigned char title_b);
// Set the background color on a native NSTextField/NSTextView, bypassing macOS system theming.
extern void mac_set_textfield_background(void *nsview_handle, unsigned char r, unsigned char g, unsigned char b);
// Replace the default blue filled selection in a wxTreeCtrl (NSOutlineView)
// with a 1px outline border, matching the Windows tree selection style.
extern void mac_set_treectrl_outline_selection(void *nsview_handle);
// Make an NSWindow non-opaque with a clear background, enabling per-pixel alpha
// transparency for custom-painted content (e.g. splash screen).
extern void mac_set_window_transparent(void *nsview_handle);
// Set a corner radius on a view's layer for clipping (e.g. rounded popup menus).
extern void mac_set_view_corner_radius(void *nsview_handle, double radius);
// Force an NSButton's title color, overriding Cocoa's state-based blue/gray rendering.
extern void mac_set_button_title_color(void *nsview_handle, unsigned char r, unsigned char g, unsigned char b);
// Control Cocoa's automatic image dimming when a button is disabled. The themed disabled glyph already
// encodes the disabled look, so dimming it again makes it nearly invisible; pass false to keep full opacity.
extern void mac_set_button_image_dims_when_disabled(void *nsview_handle, bool dims);
// Tint a push button's bezel so dialog buttons follow the active theme instead of the system blue/gray.
extern void mac_set_button_bezel_color(void *nsview_handle, unsigned char r, unsigned char g, unsigned char b);
// Disable the native blue focus ring on a view (themed inputs draw their own focused border).
extern void mac_set_focus_ring_none(void *nsview_handle);
// Set the text caret (insertion point) color so it follows the theme instead of the system blue.
extern void mac_set_insertion_point_color(void *nsview_handle, unsigned char r, unsigned char g, unsigned char b);
// Theme native table/outline selection (wxDataViewCtrl, e.g. the object list) with a fixed accent color.
// Call once at startup; the theme only changes via a full process restart.
extern void mac_install_themed_table_selection(unsigned char r, unsigned char g, unsigned char b);
#endif

} // namespace DSKY

