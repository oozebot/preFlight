///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include "GUI_Utils.hpp" // DPIDialog

#include <memory>
#include <string>
#include <vector>

class wxPanel;
class wxButton;
class wxBoxSizer;
class SpinInput;
class ComboBox;
class ScrollablePanel;

namespace DSKY
{

// Human-readable multi-line summary of a "WxH/FORMAT, ..." thumbnails value, for the Edit thumbnails
// button tooltips. Returns a localized "no thumbnails" message when the value is empty.
wxString thumbnails_summary(const std::string &value);

// Structured editor for the "thumbnails" config value. Edits a list of WxH/FORMAT entries through a
// table of themed spin controls and a format dropdown, with per-row reordering, so the user never has
// to hand-type the grammar. Order is significant: the first COLPIC entry is the large on-screen preview
// (gimage), the rest are small previews (simage). The on-disk value stays the canonical
// "WxH/FORMAT, ..." string.
class ThumbnailsDialog : public DPIDialog
{
public:
    ThumbnailsDialog(wxWindow *parent, const std::string &value);

    // Canonical "WxH/FORMAT, ..." string assembled from the table (valid only after wxID_OK).
    std::string get_value() const { return m_output; }

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    struct Row
    {
        wxPanel *panel{nullptr};
        SpinInput *width{nullptr};
        SpinInput *height{nullptr};
        ComboBox *format{nullptr};
        wxButton *up{nullptr};
        wxButton *down{nullptr};
    };

    void add_row(int width, int height, const std::string &format);
    void remove_row(const Row *row);
    void move_row(const Row *row, int direction);
    void relayout_rows();
    std::string serialize() const;

    ScrollablePanel *m_rows_panel{nullptr};
    wxBoxSizer *m_rows_sizer{nullptr};
    std::vector<std::unique_ptr<Row>> m_rows;
    std::vector<std::string> m_formats; // format names shown in the dropdown (enum order)
    std::string m_output;
};

} // namespace DSKY

