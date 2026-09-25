///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2021 - 2022 Oleksandra Iushchenko @YuSanka
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <wx/bmpcbox.h>
#include <wx/gdicmn.h>

#include "Widgets/ComboBox.hpp"

#include "GUI_Utils.hpp"

// ---------------------------------
// ***  BitmapComboBox  ***
// ---------------------------------
namespace DSKY
{

// BitmapComboBox used to presets list on Sidebar and Tabs
//class BitmapComboBox : public wxBitmapComboBox
class BitmapComboBox : public ::ComboBox
{
public:
    BitmapComboBox(wxWindow *parent, wxWindowID id = wxID_ANY, const wxString &value = wxEmptyString,
                   const wxPoint &pos = wxDefaultPosition, const wxSize &size = wxDefaultSize, int n = 0,
                   const wxString choices[] = NULL, long style = 0);
};

} // namespace DSKY
