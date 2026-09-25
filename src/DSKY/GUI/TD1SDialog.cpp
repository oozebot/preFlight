///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "TD1SDialog.hpp"

#include <wx/button.h>
#include <wx/dcmemory.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/statline.h>
#include <wx/stattext.h>

#include "I18N.hpp"
#include "GUI_App.hpp"
#include "Tab.hpp"
#include "Widgets/UIColors.hpp"
#include "luminary/presets/bundle/PresetBundle.hpp"

namespace DSKY
{
using namespace Luminary;

TD1SDialog::TD1SDialog(wxWindow *parent, const ColorRGB &color, float td, const std::string &hex_color)
    : wxDialog(parent, wxID_ANY, _L("TD1S Filament Detected"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    , m_td(td)
{
    int em = wxGetApp().em_unit();
    auto *main_sizer = new wxBoxSizer(wxVERTICAL);

    // Measurement section
    auto *measure_sizer = new wxBoxSizer(wxHORIZONTAL);

    // Color swatch
    int swatch_size = em * 6;
    wxBitmap swatch_bmp(swatch_size, swatch_size);
    {
        wxMemoryDC dc(swatch_bmp);
        dc.SetBrush(wxBrush(wxColour((unsigned char) (color.r() * 255.0f), (unsigned char) (color.g() * 255.0f),
                                     (unsigned char) (color.b() * 255.0f))));
        dc.SetPen(wxPen(UIColors::StaticBoxBorder(), 1)); // themed swatch outline
        dc.DrawRectangle(0, 0, swatch_size, swatch_size);
    }
    auto *swatch_img = new wxStaticBitmap(this, wxID_ANY, swatch_bmp);
    measure_sizer->Add(swatch_img, 0, wxALL | wxALIGN_CENTER_VERTICAL, em);

    // TD value + color info
    auto *info_sizer = new wxBoxSizer(wxVERTICAL);

    // TD value (large, prominent)
    auto *td_label = new wxStaticText(this, wxID_ANY, wxString::Format("TD: %.1f mm", td));
    td_label->SetFont(wxGetApp().bold_font().Scaled(1.5f));
    info_sizer->Add(td_label, 0, wxBOTTOM, em / 2);

    // Color info (small, informational)
    auto *color_note = new wxStaticText(this, wxID_ANY,
                                        wxString::Format(_L("Transmission color: #%s (for reference only)"),
                                                         hex_color));
    color_note->SetFont(wxGetApp().normal_font());
    color_note->SetForegroundColour(UIColors::SecondaryText()); // themed informational text
    info_sizer->Add(color_note, 0);

    measure_sizer->Add(info_sizer, 1, wxALL | wxALIGN_CENTER_VERTICAL, em);
    main_sizer->Add(measure_sizer, 0, wxEXPAND);

    // Separator
    main_sizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, em);

    // Filament preset picker
    auto *assign_sizer = new wxBoxSizer(wxVERTICAL);

    auto *assign_label = new wxStaticText(this, wxID_ANY, _L("Apply TD to filament profile:"));
    assign_label->SetFont(wxGetApp().bold_font());
    assign_sizer->Add(assign_label, 0, wxBOTTOM, em / 2);

    // List all user filament presets (not per-extruder, to avoid duplicates)
    m_preset_choice = new wxChoice(this, wxID_ANY);
    auto *preset_bundle = wxGetApp().preset_bundle;
    if (preset_bundle)
    {
        const auto &presets = preset_bundle->filaments.get_presets();
        for (size_t i = 0; i < presets.size(); ++i)
        {
            const Preset &preset = presets[i];
            // Skip default/hidden presets
            if (!preset.is_visible || preset.is_default)
                continue;

            m_preset_choice->Append(wxString::FromUTF8(preset.name.c_str()));
        }
    }

    if (m_preset_choice->GetCount() > 0)
        m_preset_choice->SetSelection(0);

    assign_sizer->Add(m_preset_choice, 0, wxEXPAND);
    main_sizer->Add(assign_sizer, 0, wxEXPAND | wxALL, em);

    // Buttons
    auto *btn_sizer = new wxBoxSizer(wxHORIZONTAL);

    auto *btn_dismiss = new wxButton(this, wxID_CANCEL, _L("Dismiss"));
    btn_sizer->Add(btn_dismiss, 0, wxALL, em / 2);

    btn_sizer->AddStretchSpacer();

    auto *btn_apply = new wxButton(this, wxID_APPLY, _L("Apply TD"));
    btn_apply->SetDefault();
    btn_sizer->Add(btn_apply, 0, wxALL, em / 2);

    main_sizer->Add(btn_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, em / 2);

    SetSizerAndFit(main_sizer);
    CenterOnParent();

    btn_apply->Bind(wxEVT_BUTTON, &TD1SDialog::on_apply, this);
    btn_dismiss->Bind(wxEVT_BUTTON, &TD1SDialog::on_dismiss, this);
}

void TD1SDialog::on_apply(wxCommandEvent &)
{
    int sel = m_preset_choice->GetSelection();
    if (sel == wxNOT_FOUND)
    {
        EndModal(wxID_CANCEL);
        return;
    }

    auto *preset_bundle = wxGetApp().preset_bundle;
    if (!preset_bundle)
    {
        EndModal(wxID_CANCEL);
        return;
    }

    // Select the chosen preset for editing
    std::string preset_name = m_preset_choice->GetString(sel).ToUTF8().data();
    Tab *filament_tab = wxGetApp().get_tab(Preset::TYPE_FILAMENT);
    if (filament_tab)
        filament_tab->select_preset(preset_name);

    // Write TD to the now-active filament preset
    DynamicPrintConfig &config = preset_bundle->filaments.get_edited_preset().config;
    auto *td_opt = config.option<ConfigOptionFloats>("filament_transmission_distance", true);
    if (td_opt)
    {
        if (td_opt->values.empty())
            td_opt->values.push_back(4.0);
        td_opt->values[0] = (double) m_td;

        if (filament_tab)
        {
            filament_tab->reload_config();
            filament_tab->update_dirty();
            // Save silently using the existing preset name (no dialog)
            filament_tab->save_preset(preset_name);
        }
    }

    EndModal(wxID_APPLY);
}

void TD1SDialog::on_dismiss(wxCommandEvent &)
{
    EndModal(wxID_CANCEL);
}

} // namespace DSKY
