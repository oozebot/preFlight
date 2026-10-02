///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include <wx/font.h>

#ifdef _WIN32
#include <wx/msw/wrapwin.h>
#endif

namespace DSKY
{
namespace GdiCache
{

// A font equal to `font` (same native description) that every caller shares, so equal fonts
// derived per widget hold one native handle between them instead of one each. Kept until
// release_all().
const wxFont &shared_font(const wxFont &font);

#ifdef _WIN32
// A solid brush of this colour shared by the whole process, for the WM_CTLCOLOR* answers of the
// input widgets. Created on first use and kept until release_all(); callers never delete it. A
// process uses a handful of theme colours, so the cache holds tens of brushes, not one per widget.
// NULL when Windows refuses the brush (counted as GUI_SHARED_BRUSH_FAILED).
HBRUSH shared_solid_brush(COLORREF colour);
#endif

// Releases every cached font and brush. Called once, at application teardown, after the windows
// are gone.
void release_all();

} // namespace GdiCache
} // namespace DSKY
