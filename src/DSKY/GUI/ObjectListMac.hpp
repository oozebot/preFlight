///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include <wx/gdicmn.h>
#include <wx/string.h>

class wxWindow;

namespace DSKY
{
// Gives the data view's native outline view a tracking area, so the list receives mouse-moved
// events whether or not it holds keyboard focus.
void mac_track_mouse_moves(wxWindow *data_view);

// Shows the system-styled tooltip panel with its top left at a wx screen position, or hides it.
void mac_tooltip_show(const wxString &text, const wxPoint &screen_pos);
void mac_tooltip_hide();
} // namespace DSKY
