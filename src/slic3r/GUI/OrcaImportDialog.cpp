///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "OrcaImportDialog.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MsgDialog.hpp"
#include "Widgets/TextInput.hpp"
#include "Widgets/UIColors.hpp"

#include "libslic3r/OrcaConfigImporter.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/miniz_extension.hpp"

#include <boost/filesystem.hpp>

#include <miniz.h>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/filedlg.h>
#include <wx/sizer.h>
#include <wx/panel.h>
#include <wx/stattext.h>

namespace Slic3r
{
namespace GUI
{

// -----------------------------------------------------------------------
// OrcaImportResultsDialog
// -----------------------------------------------------------------------

OrcaImportResultsDialog::OrcaImportResultsDialog(wxWindow *parent, const OrcaConfigImporter::ImportResult &result)
    : DPIDialog(parent, wxID_ANY, _L("OrcaSlicer Import Results"), wxDefaultPosition, wxSize(700, 700),
                wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    build_ui(result);

    // Apply dark theme: dark title bar, size grip removal, button theming
    wxGetApp().UpdateDlgDarkUI(this);

    // Re-apply custom colors that UpdateDlgDarkUI overrides with standard theme
    apply_theme_overrides();
}

void OrcaImportResultsDialog::on_dpi_changed(const wxRect &)
{
    if (m_scroll)
        m_scroll->msw_rescale();
    Fit();
    Refresh();
}

void OrcaImportResultsDialog::on_sys_color_changed()
{
    wxGetApp().UpdateDlgDarkUI(this);
    if (m_scroll)
        m_scroll->sys_color_changed();
    apply_theme_overrides();
    Refresh();
}

void OrcaImportResultsDialog::apply_theme_overrides()
{
    // Dialog uses PanelBackground (darker than InputBackground set by UpdateDlgDarkUI)
    SetBackgroundColour(UIColors::PanelBackground());
    SetForegroundColour(UIColors::PanelForeground());

    if (m_scroll)
    {
        // Reset scroll content panel and label backgrounds to match PanelBackground
        m_scroll->SetBackgroundColour(UIColors::PanelBackground());
        wxWindow *content = m_scroll->GetContentPanel();
        if (content)
        {
            content->SetBackgroundColour(UIColors::PanelBackground());
            for (auto *child : content->GetChildren())
            {
                if (dynamic_cast<wxStaticText *>(child) || dynamic_cast<wxPanel *>(child))
                    child->SetBackgroundColour(UIColors::PanelBackground());
            }
        }
    }

    // Error labels need red foreground
    for (auto *label : m_error_labels)
        label->SetForegroundColour(wxColour(255, 80, 80));

    // Section headers use dimmer secondary text for visual hierarchy
    for (auto *label : m_section_labels)
        label->SetForegroundColour(UIColors::SecondaryText());

    // OK button: let UpdateDlgDarkUI handle theming (matches standard dialog buttons)
}

void OrcaImportResultsDialog::build_ui(const OrcaConfigImporter::ImportResult &result)
{
    wxBoxSizer *outer_sizer = new wxBoxSizer(wxVERTICAL);

    // Custom scrollable panel with themed scrollbar
    m_scroll = new ScrollablePanel(this, wxID_ANY);
    wxWindow *content = m_scroll->GetContentPanel();

    wxBoxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);

    // --- Errors section (at top, if any) ---
    if (!result.errors.empty())
    {
        wxStaticText *error_label = new wxStaticText(content, wxID_ANY, _L("Errors:"));
        error_label->SetFont(error_label->GetFont().Bold());
        m_error_labels.push_back(error_label);
        main_sizer->Add(error_label, 0, wxALL, 8);

        for (const auto &err : result.errors)
        {
            wxStaticText *item = new wxStaticText(content, wxID_ANY, wxString::FromUTF8(err));
            m_error_labels.push_back(item);
            main_sizer->Add(item, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
        }

        // Themed separator
        wxPanel *sep = new wxPanel(content, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
        sep->SetBackgroundColour(UIColors::HeaderDivider());
        main_sizer->Add(sep, 0, wxEXPAND | wxALL, 5);
    }

    // --- Section 1: Imported successfully ---
    {
        wxStaticText *summary_label = new wxStaticText(content, wxID_ANY, _L("Imported Successfully"));
        summary_label->SetFont(summary_label->GetFont().Bold());
        main_sizer->Add(summary_label, 0, wxALL, 8);

        auto add_count = [&](const wxString &type, const std::vector<std::string> &names)
        {
            if (names.empty())
                return;
            wxString text = wxString::Format("  %s: %zu", type, names.size());
            for (const auto &n : names)
                text += "\n    - " + wxString::FromUTF8(n);
            wxStaticText *item = new wxStaticText(content, wxID_ANY, text);
            main_sizer->Add(item, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
        };

        add_count(_L("Printer profiles"), result.imported_printers);
        add_count(_L("Filament profiles"), result.imported_filaments);
        add_count(_L("Process profiles"), result.imported_prints);

        if (result.imported_printers.empty() && result.imported_filaments.empty() && result.imported_prints.empty())
        {
            wxStaticText *none = new wxStaticText(content, wxID_ANY, _L("  No profiles were imported."));
            main_sizer->Add(none, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
        }
    }

    // --- Section 2: Imported with changes (lossy mappings) ---
    if (!result.lossy_mappings.empty())
        add_section(main_sizer, content, _L("Imported with Changes"), result.lossy_mappings, 100, true);

    // --- Section 3: Dropped (no equivalent) ---
    if (!result.dropped_keys.empty())
        add_section(main_sizer, content, _L("Dropped Settings (No preFlight Equivalent)"), result.dropped_keys, 120);

    // --- Section 4: GCode warnings ---
    if (!result.gcode_warnings.empty())
        add_section(main_sizer, content, _L("GCode Warnings"), result.gcode_warnings, 80);

    m_scroll->SetContentSizer(main_sizer);
    outer_sizer->Add(m_scroll, 1, wxEXPAND | wxALL, 5);

    // --- OK button (outside scroll area, pinned at bottom) ---
    wxBoxSizer *btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    btn_sizer->AddStretchSpacer();
    m_ok_btn = new wxButton(this, wxID_OK, _L("OK"));
    btn_sizer->Add(m_ok_btn, 0, wxALL, 8);
    outer_sizer->Add(btn_sizer, 0, wxEXPAND | wxBOTTOM | wxRIGHT, 5);

    SetSizer(outer_sizer);
    CenterOnParent();
}

void OrcaImportResultsDialog::add_section(wxSizer *parent_sizer, wxWindow *parent, const wxString &title,
                                          const std::vector<std::string> &items, int height, bool double_space)
{
    // Section label
    wxString label = wxString::Format("%s (%zu)", title, items.size());
    wxStaticText *section_label = new wxStaticText(parent, wxID_ANY, label);
    section_label->SetFont(section_label->GetFont().Bold());
    m_section_labels.push_back(section_label);
    parent_sizer->Add(section_label, 0, wxLEFT | wxRIGHT | wxTOP, 8);

    // Themed read-only text area with custom scrollbar
    wxString separator = double_space ? "\n\n" : "\n";
    wxString all_text;
    for (const auto &item : items)
    {
        if (!all_text.empty())
            all_text += separator;
        all_text += wxString::FromUTF8(item);
    }

    auto *text_input = new ::TextInput(parent, all_text, "", "", wxDefaultPosition, wxSize(-1, height),
                                       wxTE_MULTILINE | wxTE_READONLY);
    wxGetApp().UpdateDarkUI(text_input);
    parent_sizer->Add(text_input, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
}

// -----------------------------------------------------------------------
// Top-level import workflow
// -----------------------------------------------------------------------

void import_orca_bundle(wxWindow *parent)
{
    // Step 1: Check for unsaved preset changes
    if (!wxGetApp().check_and_save_current_preset_changes(_L("Importing OrcaSlicer bundle"), "", false))
        return;

    // Step 2: File picker
    wxFileDialog file_dlg(
        parent, _L("Select OrcaSlicer bundle to import:"), wxGetApp().app_config->get_last_dir(), "",
        "OrcaSlicer bundles (*.orca_printer;*.orca_filament;*.zip)|*.orca_printer;*.orca_filament;*.zip",
        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (file_dlg.ShowModal() != wxID_OK)
        return;

    wxString file_path = file_dlg.GetPath();
    wxGetApp().app_config->update_config_dir(boost::filesystem::path(file_path.ToUTF8().data()).parent_path().string());

    // Step 3: Quick-read the manifest to show a preview dialog
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!open_zip_reader(&zip, file_path.ToUTF8().data()))
    {
        show_error(parent, _L("Failed to open the selected file as a ZIP archive."));
        return;
    }

    // Read manifest
    std::string manifest_json;
    {
        int idx = mz_zip_reader_locate_file(&zip, "bundle_structure.json", nullptr, 0);
        if (idx >= 0)
        {
            size_t size = 0;
            void *data = mz_zip_reader_extract_to_heap(&zip, (mz_uint) idx, &size, 0);
            if (data)
            {
                manifest_json.assign(static_cast<const char *>(data), size);
                mz_free(data);
            }
        }
    }
    close_zip_reader(&zip);

    if (manifest_json.empty())
    {
        show_error(parent,
                   _L("No bundle_structure.json found in the archive. This may not be a valid OrcaSlicer bundle."));
        return;
    }

    auto manifest = OrcaConfigImporter::parse_manifest(manifest_json);

    // Step 4: Show import options dialog
    wxDialog options_dlg(parent, wxID_ANY, _L("Import OrcaSlicer Bundle"), wxDefaultPosition, wxDefaultSize,
                         wxDEFAULT_DIALOG_STYLE);
    wxBoxSizer *dlg_sizer = new wxBoxSizer(wxVERTICAL);

    // Bundle info
    wxString info = wxString::Format(_L("Bundle: %s\nVersion: %s"), wxString::FromUTF8(manifest.printer_preset_name),
                                     wxString::FromUTF8(manifest.version));
    dlg_sizer->Add(new wxStaticText(&options_dlg, wxID_ANY, info), 0, wxALL, 10);

    wxPanel *sep = new wxPanel(&options_dlg, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    sep->SetBackgroundColour(UIColors::HeaderDivider());
    dlg_sizer->Add(sep, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

    // Checkboxes for what to import
    wxCheckBox *chk_printer = new wxCheckBox(&options_dlg, wxID_ANY,
                                             wxString::Format(_L("Printer profiles (%zu)"),
                                                              manifest.printer_configs.size()));
    chk_printer->SetValue(!manifest.printer_configs.empty());
    chk_printer->Enable(!manifest.printer_configs.empty());
    dlg_sizer->Add(chk_printer, 0, wxALL, 10);

    wxCheckBox *chk_filament = new wxCheckBox(&options_dlg, wxID_ANY,
                                              wxString::Format(_L("Filament profiles (%zu)"),
                                                               manifest.filament_configs.size()));
    chk_filament->SetValue(!manifest.filament_configs.empty());
    chk_filament->Enable(!manifest.filament_configs.empty());
    dlg_sizer->Add(chk_filament, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

    wxCheckBox *chk_process = new wxCheckBox(&options_dlg, wxID_ANY,
                                             wxString::Format(_L("Process profiles (%zu)"),
                                                              manifest.process_configs.size()));
    chk_process->SetValue(!manifest.process_configs.empty());
    chk_process->Enable(!manifest.process_configs.empty());
    dlg_sizer->Add(chk_process, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

    wxBoxSizer *opt_btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    opt_btn_sizer->AddStretchSpacer();
    opt_btn_sizer->Add(new wxButton(&options_dlg, wxID_OK, _L("Import")), 0, wxRIGHT, 5);
    opt_btn_sizer->Add(new wxButton(&options_dlg, wxID_CANCEL), 0);
    dlg_sizer->Add(opt_btn_sizer, 0, wxEXPAND | wxALL, 10);

    options_dlg.SetSizer(dlg_sizer);
    dlg_sizer->SetSizeHints(&options_dlg);
    options_dlg.CenterOnParent();

    // Apply dark theme: dark title bar, button theming, size grip removal
    wxGetApp().UpdateDlgDarkUI(&options_dlg);

    if (options_dlg.ShowModal() != wxID_OK)
        return;

    // Step 5: Run the import
    OrcaConfigImporter::ImportOptions opts;
    opts.import_printer = chk_printer->GetValue();
    opts.import_filaments = chk_filament->GetValue();
    opts.import_processes = chk_process->GetValue();

    if (!opts.import_printer && !opts.import_filaments && !opts.import_processes)
    {
        show_info(parent, _L("Nothing selected to import."), _L("Info"));
        return;
    }

    OrcaConfigImporter importer;
    auto confirm_overwrite = [parent](const std::string &name) -> int
    {
        wxString msg = wxString::Format(_L("A preset named '%s' already exists. Do you want to overwrite it?"),
                                        wxString::FromUTF8(name));
        MessageDialog dialog(parent, msg, _L("Overwrite Preset?"), wxYES_NO | wxCANCEL | wxICON_QUESTION);
        int result = dialog.ShowModal();
        if (result == wxID_YES)
            return 1;
        if (result == wxID_NO)
            return 0;
        return -1; // Cancel
    };

    OrcaConfigImporter::ImportResult import_result;
    try
    {
        import_result = importer.import_bundle(file_path.ToUTF8().data(), *wxGetApp().preset_bundle, opts,
                                               confirm_overwrite);
    }
    catch (const std::exception &ex)
    {
        show_error(parent, wxString::FromUTF8(ex.what()));
        return;
    }

    // Step 6: Rebuild extruder filaments and reload presets into the GUI.
    // The import added new presets to the collections, but ExtruderFilaments
    // still has the old count. Must resync before load_current_presets iterates them.
    try
    {
        wxGetApp().preset_bundle->update_compatible(PresetSelectCompatibleType::Never);
        wxGetApp().load_current_presets();
    }
    catch (const std::exception &ex)
    {
        import_result.errors.push_back(std::string("Warning during preset reload: ") + ex.what());
    }

    // Step 7: Show results dialog
    OrcaImportResultsDialog results_dlg(parent, import_result);
    results_dlg.ShowModal();
}

} // namespace GUI
} // namespace Slic3r
