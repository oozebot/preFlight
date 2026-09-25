///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <wx/tglbtn.h>
#include <stddef.h>
#include <wx/defs.h>
#include <wx/string.h>
#include <cstddef>

class wxWindow;

class BitmapToggleButton : public wxBitmapToggleButton
{
    virtual void update() = 0;

public:
    BitmapToggleButton(wxWindow *parent = NULL, const wxString &label = wxEmptyString, wxWindowID id = wxID_ANY);

protected:
    void update_size();
};

