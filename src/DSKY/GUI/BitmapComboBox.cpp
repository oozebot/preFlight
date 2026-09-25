///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2021 - 2022 Oleksandra Iushchenko @YuSanka
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "BitmapComboBox.hpp"

#include <cstddef>
#include <vector>
#include <string>
#include <boost/algorithm/string.hpp>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/statbox.h>
#include <wx/colordlg.h>
#include <wx/wupdlock.h>
#include <wx/menu.h>
#include <wx/odcombo.h>
#include <wx/listbook.h>
#include <wx/window.h>

#ifdef _WIN32
#include <wx/msw/dcclient.h>
#include <wx/msw/private.h>
#define _MSW_DARK_MODE
#include "DarkMode.hpp"
#endif

#include "luminary/core/Prelude.hpp"
#include "luminary/config/catalog/PrintConfig.hpp"
#include "luminary/presets/bundle/PresetBundle.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include "Widgets/UIColors.hpp"

// A workaround for a set of issues related to text fitting into gtk widgets:
// Fix for bitmap combo box appearance
#if defined(__WXGTK20__) || defined(__WXGTK3__)
#include <glib-2.0/glib-object.h>
#include <pango-1.0/pango/pango-layout.h>
#include <gtk/gtk.h>
#endif

using DSKY::format_wxstr;

// ---------------------------------
// ***  BitmapComboBox  ***
// ---------------------------------

namespace DSKY
{
using namespace Luminary;

BitmapComboBox::BitmapComboBox(wxWindow *parent, wxWindowID id /* = wxID_ANY*/,
                               const wxString &value /* = wxEmptyString*/, const wxPoint &pos /* = wxDefaultPosition*/,
                               const wxSize &size /* = wxDefaultSize*/, int n /* = 0*/,
                               const wxString choices[] /* = NULL*/, long style /* = 0*/)
    : //    wxBitmapComboBox(parent, id, value, pos, size, n, choices, style)
    ::ComboBox(parent, id, value, pos, size, n, choices, style | DD_NO_CHECK_ICON)
{
    SetFont(DSKY::wxGetApp().normal_font());
}

} // namespace DSKY
