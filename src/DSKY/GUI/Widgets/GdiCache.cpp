///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "GdiCache.hpp"

#include "luminary/core/diagnostics/DebugCounters.hpp"

#include <boost/log/trivial.hpp>

#include <map>
#include <unordered_map>

namespace DSKY
{
namespace GdiCache
{

// Both caches are used from the UI thread only (widget construction and WM_CTLCOLOR* handlers),
// so they need no lock

static std::map<wxString, wxFont> &fonts()
{
    static std::map<wxString, wxFont> map;
    return map;
}

const wxFont &shared_font(const wxFont &font)
{
    if (!font.IsOk())
        return font;
    auto &map = fonts();
    const wxString key = font.GetNativeFontInfoDesc();
    auto it = map.find(key);
    if (it == map.end())
        it = map.emplace(key, font).first;
    return it->second;
}

#ifdef _WIN32
static std::unordered_map<COLORREF, HBRUSH> &brushes()
{
    static std::unordered_map<COLORREF, HBRUSH> map;
    return map;
}

HBRUSH shared_solid_brush(COLORREF colour)
{
    auto &map = brushes();
    auto it = map.find(colour);
    if (it != map.end())
        return it->second;
    HBRUSH brush = ::CreateSolidBrush(colour);
    if (brush == NULL)
    {
        // The edit control then paints with the system's colours; retried on the next paint
        DBG_COUNT_LOAD("GUI_SHARED_BRUSH_FAILED");
        BOOST_LOG_TRIVIAL(error) << "CreateSolidBrush failed for colour 0x" << std::hex << colour
                                 << "; the GDI object quota may be exhausted";
        return NULL;
    }
    map.emplace(colour, brush);
    return brush;
}
#endif

void release_all()
{
    fonts().clear();
#ifdef _WIN32
    for (auto &[colour, brush] : brushes())
        ::DeleteObject(brush);
    brushes().clear();
#endif
}

} // namespace GdiCache
} // namespace DSKY
