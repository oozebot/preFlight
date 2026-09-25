///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "ThumbnailsDialog.hpp"

#include <wx/button.h>
#include <wx/combobox.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <algorithm>
#include <string>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "wxExtensions.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/DropDown.hpp"
#include "Widgets/ScrollablePanel.hpp"
#include "Widgets/SpinInput.hpp"
#include "Widgets/UIColors.hpp"
#include "luminary/config/catalog/PrintConfig.hpp"

namespace DSKY
{
using namespace Luminary;

// Thumbnail dimensions are validated as 0 < value < 1000 elsewhere, so clamp the spinners to that range.
static constexpr int THUMB_MIN = 1;
static constexpr int THUMB_MAX = 999;
static constexpr int THUMB_DEFAULT = 160;

// Paint a panel/label with the warm theme background (and matching foreground for text).
static void theme_panel(wxWindow *win)
{
    win->SetBackgroundColour(UIColors::PanelBackground());
    win->SetForegroundColour(UIColors::PanelForeground());
}

// Lenient parse of a single "WxH/FORMAT" token. Returns false if width/height are missing or non-numeric.
// The format part is optional and may be unknown; the caller maps it onto the dropdown.
static bool parse_entry(const std::string &token, int &width, int &height, std::string &format)
{
    const auto x_pos = token.find('x');
    if (x_pos == std::string::npos)
        return false;

    std::string w_str = token.substr(0, x_pos);
    std::string rest = token.substr(x_pos + 1);
    boost::trim(w_str);
    boost::trim(rest);

    std::string h_str = rest;
    format.clear();
    if (const auto slash_pos = rest.find('/'); slash_pos != std::string::npos)
    {
        h_str = rest.substr(0, slash_pos);
        format = rest.substr(slash_pos + 1);
        boost::trim(h_str);
        boost::trim(format);
        boost::to_upper(format);
    }

    try
    {
        width = std::stoi(w_str);
        height = std::stoi(h_str);
    }
    catch (...)
    {
        return false;
    }
    return width > 0 && height > 0;
}

wxString thumbnails_summary(const std::string &value)
{
    std::string trimmed = value;
    boost::trim(trimmed);
    if (trimmed.empty())
        return _L("No thumbnails defined");

    wxString out = _L("Current thumbnails:");
    bool any = false;
    std::vector<std::string> entries;
    boost::split(entries, trimmed, boost::is_any_of(","));
    for (std::string entry : entries)
    {
        boost::trim(entry);
        if (entry.empty())
            continue;
        int w = 0, h = 0;
        std::string fmt;
        if (!parse_entry(entry, w, h, fmt))
            continue;
        out += wxString::Format("\n  %d x %d", w, h);
        if (!fmt.empty())
            out += "    " + from_u8(fmt);
        any = true;
    }
    return any ? out : _L("No thumbnails defined");
}

ThumbnailsDialog::ThumbnailsDialog(wxWindow *parent, const std::string &value)
    // No wxRESIZE_BORDER: a fixed-size dialog avoids the native corner resize grip; the scroll area
    // handles long thumbnail lists.
    : DPIDialog(parent, wxID_ANY, _(L("Edit G-code thumbnails")), wxDefaultPosition, wxDefaultSize,
                wxDEFAULT_DIALOG_STYLE)
{
    SetFont(wxGetApp().normal_font());
    SetBackgroundColour(UIColors::PanelBackground());

    // Format dropdown options are taken straight from the enum, so new formats appear automatically.
    const auto &names = ConfigOptionEnum<GCodeThumbnailsFormat>::get_enum_names();
    m_formats.assign(names.begin(), names.end());

    const int em = wxGetApp().em_unit();
    auto *main_sizer = new wxBoxSizer(wxVERTICAL);

    auto *hint = new wxStaticText(this, wxID_ANY,
                                  _(L("Each row is one preview embedded in the G-code. Width and height are in pixels; "
                                      "the format must match what the printer firmware expects. Order matters: the "
                                      "first COLPIC entry is the large on-screen preview.")));
    hint->Wrap(44 * em);
    theme_panel(hint);
    main_sizer->Add(hint, 0, wxALL, em);

    // Column headers above the table.
    auto *header_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto add_header = [&](const wxString &text, int width)
    {
        auto *label = new wxStaticText(this, wxID_ANY, text, wxDefaultPosition, wxSize(width, -1));
        theme_panel(label);
        header_sizer->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 2);
    };
    add_header(_(L("Width")), 7 * em);
    add_header(wxString(" "), 1 * em);
    add_header(_(L("Height")), 7 * em);
    add_header(_(L("Format")), 9 * em);
    main_sizer->Add(header_sizer, 0, wxLEFT | wxRIGHT, em);

    // Custom scrollable panel (themed scrollbar + warm background), not a native wxScrolledWindow.
    m_rows_panel = new ScrollablePanel(this, wxID_ANY);
    m_rows_panel->SetMinSize(wxSize(46 * em, 14 * em));
    m_rows_panel->SetBackgroundColour(UIColors::PanelBackground());
    theme_panel(m_rows_panel->GetContentPanel());
    m_rows_sizer = new wxBoxSizer(wxVERTICAL);
    m_rows_panel->SetSizer(m_rows_sizer); // delegates to the content panel
    main_sizer->Add(m_rows_panel, 1, wxEXPAND | wxLEFT | wxRIGHT, em);

    // Populate the table from the incoming value. Unknown formats fall back to the first option.
    std::vector<std::string> tokens;
    boost::split(tokens, value, boost::is_any_of(","));
    for (std::string token : tokens)
    {
        boost::trim(token);
        if (token.empty())
            continue;
        int w = 0, h = 0;
        std::string fmt;
        if (parse_entry(token, w, h, fmt))
            add_row(w, h, fmt);
    }
    if (m_rows.empty())
        add_row(THUMB_DEFAULT, THUMB_DEFAULT, m_formats.empty() ? std::string() : m_formats.front());

    auto *add_btn = new wxButton(this, wxID_ANY, _(L("Add thumbnail")));
    wxGetApp().SetWindowVariantForButton(add_btn);
    wxGetApp().UpdateDarkUI(add_btn);
    add_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &)
                  { add_row(THUMB_DEFAULT, THUMB_DEFAULT, m_formats.empty() ? std::string() : m_formats.front()); });
    main_sizer->Add(add_btn, 0, wxALL, em);

    auto *buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    wxGetApp().SetWindowVariantForButton(buttons->GetAffirmativeButton());
    wxGetApp().SetWindowVariantForButton(buttons->GetCancelButton());
    wxGetApp().UpdateDarkUI(this->FindWindowById(wxID_OK, this));
    wxGetApp().UpdateDarkUI(this->FindWindowById(wxID_CANCEL, this));
    main_sizer->Add(buttons, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM | wxTOP, em);

    this->Bind(
        wxEVT_BUTTON,
        [this](wxCommandEvent &)
        {
            m_output = serialize();
            EndModal(wxID_OK);
        },
        wxID_OK);
    this->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) { EndModal(wxID_CANCEL); });

    wxGetApp().UpdateDlgDarkUI(this, true); // dark title bar + remove the native size grip
    SetSizer(main_sizer);
    main_sizer->SetSizeHints(this);
    this->CenterOnParent();
}

void ThumbnailsDialog::add_row(int width, int height, const std::string &format)
{
    const int em = wxGetApp().em_unit();
    wxWindow *content = m_rows_panel->GetContentPanel();
    auto row = std::make_unique<Row>();

    row->panel = new wxPanel(content, wxID_ANY);
    theme_panel(row->panel);
    auto *row_sizer = new wxBoxSizer(wxHORIZONTAL);

    row->width = new SpinInput(row->panel, wxEmptyString, "", wxDefaultPosition, wxSize(7 * em, -1),
                               wxTE_PROCESS_ENTER | wxSP_ARROW_KEYS, THUMB_MIN, THUMB_MAX,
                               std::clamp(width, THUMB_MIN, THUMB_MAX));
    row_sizer->Add(row->width, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 2);

    auto *cross = new wxStaticText(row->panel, wxID_ANY, "x");
    theme_panel(cross);
    row_sizer->Add(cross, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 2);

    row->height = new SpinInput(row->panel, wxEmptyString, "", wxDefaultPosition, wxSize(7 * em, -1),
                                wxTE_PROCESS_ENTER | wxSP_ARROW_KEYS, THUMB_MIN, THUMB_MAX,
                                std::clamp(height, THUMB_MIN, THUMB_MAX));
    row_sizer->Add(row->height, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 2);

    row->format = new ComboBox(row->panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(9 * em, -1), 0, nullptr,
                               wxCB_READONLY | DD_NO_CHECK_ICON);
    for (const std::string &name : m_formats)
        row->format->Append(from_u8(name));
    int sel = 0;
    for (size_t i = 0; i < m_formats.size(); ++i)
        if (m_formats[i] == format)
        {
            sel = int(i);
            break;
        }
    if (!m_formats.empty())
        row->format->SetSelection(sel);
    row_sizer->Add(row->format, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em);

    const Row *row_ptr = row.get();

    auto make_btn = [&](const wxString &label)
    {
        auto *btn = new wxButton(row->panel, wxID_ANY, label, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        wxGetApp().SetWindowVariantForButton(btn);
        wxGetApp().UpdateDarkUI(btn);
        return btn;
    };

    row->up = make_btn(wxString::FromUTF8("\xE2\x96\xB2")); // up triangle
    row->up->Bind(wxEVT_BUTTON, [this, row_ptr](wxCommandEvent &) { move_row(row_ptr, -1); });
    row_sizer->Add(row->up, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 4);

    row->down = make_btn(wxString::FromUTF8("\xE2\x96\xBC")); // down triangle
    row->down->Bind(wxEVT_BUTTON, [this, row_ptr](wxCommandEvent &) { move_row(row_ptr, 1); });
    row_sizer->Add(row->down, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em);

    auto *remove_btn = make_btn(_(L("Remove")));
    remove_btn->Bind(wxEVT_BUTTON, [this, row_ptr](wxCommandEvent &) { remove_row(row_ptr); });
    row_sizer->Add(remove_btn, 0, wxALIGN_CENTER_VERTICAL);

    row->panel->SetSizer(row_sizer);
    m_rows.push_back(std::move(row));

    relayout_rows();
}

void ThumbnailsDialog::remove_row(const Row *row)
{
    const auto it = std::find_if(m_rows.begin(), m_rows.end(), [row](const auto &r) { return r.get() == row; });
    if (it == m_rows.end())
        return;

    m_rows_sizer->Detach((*it)->panel);
    (*it)->panel->Destroy();
    m_rows.erase(it);

    relayout_rows();
}

void ThumbnailsDialog::move_row(const Row *row, int direction)
{
    const auto it = std::find_if(m_rows.begin(), m_rows.end(), [row](const auto &r) { return r.get() == row; });
    if (it == m_rows.end())
        return;

    const auto index = std::distance(m_rows.begin(), it);
    const auto target = index + direction;
    if (target < 0 || target >= static_cast<long>(m_rows.size()))
        return;

    std::swap(m_rows[index], m_rows[target]);
    relayout_rows();
}

void ThumbnailsDialog::relayout_rows()
{
    const int em = wxGetApp().em_unit();

    // Rebuild the sizer order from the (re)ordered vector; Clear(false) detaches without destroying.
    m_rows_sizer->Clear(false);
    for (size_t i = 0; i < m_rows.size(); ++i)
    {
        m_rows_sizer->Add(m_rows[i]->panel, 0, wxEXPAND | wxBOTTOM, em / 2);
        m_rows[i]->up->Enable(i > 0);
        m_rows[i]->down->Enable(i + 1 < m_rows.size());
    }

    m_rows_panel->GetContentPanel()->Layout();
    m_rows_panel->FitInside(); // recomputes the custom scrollbar
    Layout();
}

std::string ThumbnailsDialog::serialize() const
{
    std::string out;
    for (const auto &row : m_rows)
    {
        const int sel = row->format->GetSelection();
        if (sel < 0 || sel >= int(m_formats.size()))
            continue;
        if (!out.empty())
            out += ", ";
        out += std::to_string(row->width->GetValue()) + "x" + std::to_string(row->height->GetValue()) + "/" +
               m_formats[sel];
    }
    return out;
}

void ThumbnailsDialog::on_dpi_changed(const wxRect & /*suggested_rect*/)
{
    const int em = em_unit();
    msw_buttons_rescale(this, em, {wxID_OK, wxID_CANCEL});
    Fit();
    Refresh();
}

} // namespace DSKY
