///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include <wx/choice.h>
#include <wx/dialog.h>

#include "luminary/colour/rgb/Color.hpp"

namespace DSKY
{
using namespace Luminary;

// Modal dialog shown when the TD1S sensor detects a new filament.
// Displays the measured TD value and lets the user assign it to a filament preset.
class TD1SDialog : public wxDialog
{
public:
    TD1SDialog(wxWindow *parent, const ColorRGB &color, float td, const std::string &hex_color);

private:
    void on_apply(wxCommandEvent &event);
    void on_dismiss(wxCommandEvent &event);

    float m_td;
    wxChoice *m_preset_choice{nullptr};
};

} // namespace DSKY

