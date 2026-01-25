///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2021 - 2022 Oleksandra Iushchenko @YuSanka
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef slic3r_BitmapComboBox_hpp_
#define slic3r_BitmapComboBox_hpp_

#include <wx/bmpcbox.h>
#include <wx/gdicmn.h>

#include "Widgets/ComboBox.hpp"

#include "GUI_Utils.hpp"

// ---------------------------------
// ***  BitmapComboBox  ***
// ---------------------------------
namespace Slic3r
{
namespace GUI
{

// BitmapComboBox used to presets list on Sidebar and Tabs
//class BitmapComboBox : public wxBitmapComboBox
class BitmapComboBox : public ::ComboBox
{
public:
    BitmapComboBox(wxWindow *parent, wxWindowID id = wxID_ANY, const wxString &value = wxEmptyString,
                   const wxPoint &pos = wxDefaultPosition, const wxSize &size = wxDefaultSize, int n = 0,
                   const wxString choices[] = NULL, long style = 0);
#if 0
~BitmapComboBox();

#ifdef _WIN32
    int Append(const wxString& item);
#endif
    int Append(const wxString& item, const wxBitmapBundle& bitmap)
    {
        return wxBitmapComboBox::Append(item, bitmap);
    }

protected:

//#ifdef _WIN32
bool MSWOnDraw(WXDRAWITEMSTRUCT* item) override;
void DrawBackground_(wxDC& dc, const wxRect& rect, int WXUNUSED(item), int flags) const;
public:
void Rescale();
#endif
};

} // namespace GUI
} // namespace Slic3r
#endif
