///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "LabeledBorderPanel.hpp"
#include "../GUI_App.hpp"
#include "UIColors.hpp"

#include <wx/dcbuffer.h>

namespace Slic3r
{
namespace GUI
{

// DPI scaling helpers
static int GetScaledTopPaddingExtra()
{
    return (wxGetApp().em_unit() * 8) / 10; // 8px at 100%
}

static int GetScaledSidePadding()
{
    return (wxGetApp().em_unit() * 6) / 10; // 6px at 100%
}

static int GetScaledLabelPadding()
{
    return (wxGetApp().em_unit() * 4) / 10; // 4px at 100%
}

static int GetScaledLabelIndent()
{
    return (wxGetApp().em_unit() * 8) / 10; // 8px at 100%
}

static int GetScaledBorderWidth()
{
    return std::max(1, wxGetApp().em_unit() / 10); // 1px at 100%, min 1px
}

LabeledBorderPanel::LabeledBorderPanel(wxWindow *parent, wxWindowID id, const wxString &label, const wxPoint &pos,
                                       const wxSize &size)
    : wxPanel(parent, id, pos, size, wxTAB_TRAVERSAL | wxFULL_REPAINT_ON_RESIZE), m_label(label)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);

    UpdateColors();

    // Create inner sizer with padding for the border and label
    m_inner_sizer = new wxBoxSizer(wxVERTICAL);

    // Top padding accounts for label height + spacing (scaled for DPI)
    int label_height = GetTextExtent(m_label).GetHeight();
    int top_padding = label_height / 2 + GetScaledTopPaddingExtra(); // Half label above border, plus spacing
    int side_padding = GetScaledSidePadding();

    wxBoxSizer *outer_sizer = new wxBoxSizer(wxVERTICAL);
    outer_sizer->AddSpacer(top_padding);
    outer_sizer->Add(m_inner_sizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, side_padding);

    SetSizer(outer_sizer);

    Bind(wxEVT_PAINT, &LabeledBorderPanel::OnPaint, this);
}

void LabeledBorderPanel::SetLabel(const wxString &label)
{
    m_label = label;
    Refresh();
}

void LabeledBorderPanel::UpdateColors()
{
    bool is_dark = wxGetApp().dark_mode();

    m_border_color = is_dark ? wxColour(255, 255, 255) : wxColour(0, 0, 0);
    m_text_color = is_dark ? UIColors::InputForegroundDark() : UIColors::InputForegroundLight();
    // Use parent's background color so the border panel matches the surrounding page
    m_bg_color = GetParent() ? GetParent()->GetBackgroundColour()
                             : (is_dark ? UIColors::PanelBackgroundDark() : UIColors::PanelBackgroundLight());
}

void LabeledBorderPanel::sys_color_changed()
{
    UpdateColors();
    Refresh();
}

void LabeledBorderPanel::msw_rescale()
{
    // Recalculate layout with new DPI values
    int label_height = GetTextExtent(m_label).GetHeight();
    int top_padding = label_height / 2 + GetScaledTopPaddingExtra();
    int side_padding = GetScaledSidePadding();

    // Recreate outer sizer with new scaled values
    wxBoxSizer *outer_sizer = new wxBoxSizer(wxVERTICAL);
    outer_sizer->AddSpacer(top_padding);
    outer_sizer->Add(m_inner_sizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, side_padding);

    SetSizer(outer_sizer);
    Layout();
    Refresh();
}

void LabeledBorderPanel::OnPaint(wxPaintEvent &event)
{
    wxAutoBufferedPaintDC dc(this);
    wxSize size = GetClientSize();

    // Fill background
    dc.SetBrush(wxBrush(m_bg_color));
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRectangle(0, 0, size.x, size.y);

    // Use bold font for label
    wxFont bold_font = GetFont();
    bold_font.SetWeight(wxFONTWEIGHT_BOLD);
    dc.SetFont(bold_font);

    // Calculate label dimensions (scaled for DPI)
    wxSize label_size = dc.GetTextExtent(m_label);
    int label_padding = GetScaledLabelPadding(); // Padding on each side of label text
    int label_width = label_size.GetWidth() + label_padding * 2;
    int label_height = label_size.GetHeight();

    // Border starts at half the label height from top
    int border_top = label_height / 2;
    int border_left = 0;
    int border_right = size.x - 1;
    int border_bottom = size.y - 1;

    // Left-aligned position for label with indent (matches wxStaticBox style)
    int label_indent = GetScaledLabelIndent(); // Indent from left border
    int label_x = label_indent;
    int label_text_x = label_x + label_padding;
    int label_y = 0; // Label starts at top

    // Draw border with gap for label (scaled width)
    dc.SetPen(wxPen(m_border_color, GetScaledBorderWidth()));

    // Top border - left segment (up to label)
    dc.DrawLine(border_left, border_top, label_x, border_top);
    // Top border - right segment (after label)
    dc.DrawLine(label_x + label_width, border_top, border_right + 1, border_top);

    // Left border
    dc.DrawLine(border_left, border_top, border_left, border_bottom);
    // Right border
    dc.DrawLine(border_right, border_top, border_right, border_bottom);
    // Bottom border
    dc.DrawLine(border_left, border_bottom, border_right + 1, border_bottom);

    // Draw label text (left-aligned with indent)
    dc.SetTextForeground(m_text_color);
    dc.SetBackgroundMode(wxTRANSPARENT);
    dc.DrawText(m_label, label_text_x, label_y);
}

} // namespace GUI
} // namespace Slic3r
