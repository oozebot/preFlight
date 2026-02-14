///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include "GUI_Utils.hpp"
#include "Widgets/ScrollablePanel.hpp"

#include <string>
#include <vector>

#include "libslic3r/OrcaConfigImporter.hpp"

class wxButton;
class wxStaticText;

namespace Slic3r
{
namespace GUI
{

// Dialog shown after import completes, displaying the four-section results report.
class OrcaImportResultsDialog : public DPIDialog
{
public:
    OrcaImportResultsDialog(wxWindow *parent, const OrcaConfigImporter::ImportResult &result);

protected:
    void on_dpi_changed(const wxRect &) override;
    void on_sys_color_changed() override;

private:
    void build_ui(const OrcaConfigImporter::ImportResult &result);
    void apply_theme_overrides();

    // Add a labeled read-only text area section.
    void add_section(wxSizer *parent_sizer, wxWindow *parent, const wxString &title,
                     const std::vector<std::string> &items, int height, bool double_space = false);

    ScrollablePanel *m_scroll{nullptr};
    wxButton *m_ok_btn{nullptr};
    std::vector<wxStaticText *> m_error_labels;
    std::vector<wxStaticText *> m_section_labels;
};

// Top-level function to run the full Orca import workflow:
// 1. Show file picker
// 2. Preview manifest + let user choose what to import
// 3. Run import
// 4. Show results dialog
// Called from MainFrame menu handler.
void import_orca_bundle(wxWindow *parent);

} // namespace GUI
} // namespace Slic3r
