///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2021 - 2022 Oleksandra Iushchenko @YuSanka, Lukáš Hejl @hejllukas
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "Notebook.hpp"

#include "GUI_App.hpp"
#include "wxExtensions.hpp"

#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/dcclient.h>

wxDEFINE_EVENT(wxCUSTOMEVT_NOTEBOOK_SEL_CHANGED, wxCommandEvent);

#ifndef _WIN32
// Owner-drawn tab button used on Linux/macOS.
NotebookTabButton::NotebookTabButton(wxWindow *parent, const wxString &label, const std::string &bmp_name)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTAB_TRAVERSAL)
    , m_label(label)
    , m_icon_name(bmp_name)
    , m_bmp_bundle(bmp_name.empty() ? wxBitmapBundle() : *get_bmp_bundle(bmp_name))
{
    wxWindow::SetLabel(label);
    SetBackgroundColour(DSKY::wxGetApp().get_highlight_default_clr());
    update_min_size();

    Bind(wxEVT_PAINT, [this](wxPaintEvent &) { render(); });
    Bind(wxEVT_LEFT_UP,
         [this](wxMouseEvent &event)
         {
             wxCommandEvent evt(wxEVT_BUTTON, GetId());
             GetEventHandler()->AddPendingEvent(evt);
             event.Skip();
         });
}

void NotebookTabButton::update_min_size()
{
    int em = em_unit(this);
    int x = 0, y = 0;
    GetTextExtent(m_label.IsEmpty() ? "a" : m_label, &x, &y);
    int w = x + 4 * em;
    if (m_bmp_bundle.IsOk())
        w += m_bmp_bundle.GetPreferredBitmapSizeFor(this).GetWidth() + em;
    SetMinSize(wxSize(w, y + int(1.5 * em)));
}

void NotebookTabButton::render()
{
    const wxRect rc(GetSize());
    wxPaintDC dc(this);

    const wxColour bg = GetBackgroundColour();
    dc.SetPen(bg);
    dc.SetBrush(bg);
    dc.DrawRectangle(rc);

    int em = em_unit(this);
    wxPoint pt(0, 0);

    if (m_bmp_bundle.IsOk())
    {
        const wxBitmap bmp = m_bmp_bundle.GetBitmapFor(this);
        pt.x = m_label.IsEmpty() ? (rc.width - bmp.GetWidth()) / 2 : em;
        pt.y = (rc.height - bmp.GetHeight()) / 2;
        dc.DrawBitmap(bmp, pt, true);
        pt.x += bmp.GetWidth() + int(0.5 * em);
    }

    if (!m_label.IsEmpty())
    {
        dc.SetFont(GetFont());
        const wxSize ts = dc.GetTextExtent(m_label);
        if (!m_bmp_bundle.IsOk())
            pt.x = (rc.width - ts.x) / 2;
        pt.y = (rc.height - ts.y) / 2;
        dc.SetTextForeground(DSKY::wxGetApp().get_label_clr_default());
        dc.DrawText(m_label, pt);
    }
}

void NotebookTabButton::SetLabel(const wxString &label)
{
    m_label = label;
    wxWindow::SetLabel(label);
    update_min_size();
    Refresh();
}

bool NotebookTabButton::SetBitmap_(const std::string &bmp_name)
{
    m_icon_name = bmp_name;
    m_bmp_bundle = bmp_name.empty() ? wxBitmapBundle() : *get_bmp_bundle(bmp_name);
    update_min_size();
    Refresh();
    return true;
}

void NotebookTabButton::sys_color_changed()
{
    m_bmp_bundle = m_icon_name.empty() ? wxBitmapBundle() : *get_bmp_bundle(m_icon_name);
    Refresh();
}
#endif // !_WIN32

ButtonsListCtrl::ButtonsListCtrl(wxWindow *parent)
    : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTAB_TRAVERSAL)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#else
    // Paint the strip background from the theme palette on Linux/macOS.
    SetBackgroundColour(DSKY::wxGetApp().get_window_default_clr());
#endif //__WINDOWS__

    int em = em_unit(this); // DSKY::wxGetApp().em_unit();
    m_btn_margin = std::lround(0.3 * em);
    m_line_margin = std::lround(0.1 * em);

    m_sizer = new wxBoxSizer(wxHORIZONTAL);
    this->SetSizer(m_sizer);

    m_buttons_sizer = new wxFlexGridSizer(1, m_btn_margin, m_btn_margin);
    m_sizer->Add(m_buttons_sizer, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxBOTTOM, m_btn_margin);

    this->Bind(wxEVT_PAINT, &ButtonsListCtrl::OnPaint, this);
}

void ButtonsListCtrl::OnPaint(wxPaintEvent &)
{
    DSKY::wxGetApp().UpdateDarkUI(this);
    const wxSize sz = GetSize();
    wxPaintDC dc(this);

#ifndef _WIN32
    // Fill the strip (incl. margins/gaps) with the themed background; UpdateDarkUI is a Windows-only no-op.
    const wxColour &strip_bg = DSKY::wxGetApp().get_window_default_clr();
    dc.SetPen(strip_bg);
    dc.SetBrush(strip_bg);
    dc.DrawRectangle(wxRect(sz));
#endif

    if (m_selection < 0 || m_selection >= (int) m_pageButtons.size())
        return;

    const wxColour &selected_btn_bg = DSKY::wxGetApp().get_color_selected_btn_bg();
    const wxColour &default_btn_bg = DSKY::wxGetApp().get_highlight_default_clr();
    const wxColour &btn_marker_color = DSKY::wxGetApp().get_color_hovered_btn_label();

    // highlight selected notebook button

    for (int idx = 0; idx < int(m_pageButtons.size()); idx++)
    {
        auto *btn = m_pageButtons[idx];

        btn->SetBackgroundColour(idx == m_selection ? selected_btn_bg : default_btn_bg);
#ifndef _WIN32
        // Owner-drawn buttons repaint from their background colour; nudge them after a change.
        btn->Refresh();
#endif

        wxPoint pos = btn->GetPosition();
        wxSize size = btn->GetSize();
        const wxColour &clr = idx == m_selection ? btn_marker_color : default_btn_bg;
        dc.SetPen(clr);
        dc.SetBrush(clr);
        dc.DrawRectangle(pos.x, pos.y + size.y, size.x, sz.y - size.y);
    }

    // Draw orange bottom line

    dc.SetPen(btn_marker_color);
    dc.SetBrush(btn_marker_color);
    dc.DrawRectangle(1, sz.y - m_line_margin, sz.x, m_line_margin);
}

void ButtonsListCtrl::Rescale()
{
    int em = em_unit(this);
    m_btn_margin = std::lround(0.3 * em);
    m_line_margin = std::lround(0.1 * em);
    m_buttons_sizer->SetVGap(m_btn_margin);
    m_buttons_sizer->SetHGap(m_btn_margin);

    m_sizer->Layout();
}

void ButtonsListCtrl::OnColorsChanged()
{
    for (auto *btn : m_pageButtons)
        btn->sys_color_changed();

    m_sizer->Layout();
}

void ButtonsListCtrl::SetSelection(int sel)
{
    if (m_selection == sel)
        return;
    m_selection = sel;
    Refresh();
}

bool ButtonsListCtrl::InsertPage(size_t n, const wxString &text, bool bSelect /* = false*/,
                                 const std::string &bmp_name /* = ""*/)
{
#ifdef _WIN32
    ScalableButton *btn = new ScalableButton(this, wxID_ANY, bmp_name, text, wxDefaultSize, wxDefaultPosition,
                                             wxBU_EXACTFIT | wxNO_BORDER | (bmp_name.empty() ? 0 : wxBU_LEFT));
#else
    NotebookTabButton *btn = new NotebookTabButton(this, text, bmp_name);
#endif
    btn->Bind(wxEVT_BUTTON,
              [this, btn](wxCommandEvent &event)
              {
                  if (auto it = std::find(m_pageButtons.begin(), m_pageButtons.end(), btn); it != m_pageButtons.end())
                  {
                      m_selection = it - m_pageButtons.begin();
                      wxCommandEvent evt = wxCommandEvent(wxCUSTOMEVT_NOTEBOOK_SEL_CHANGED);
                      evt.SetId(m_selection);
                      wxPostEvent(this->GetParent(), evt);
                      Refresh();
                  }
              });
    DSKY::wxGetApp().UpdateDarkUI(btn);
    m_pageButtons.insert(m_pageButtons.begin() + n, btn);
    m_buttons_sizer->Insert(n, new wxSizerItem(btn));
    m_buttons_sizer->SetCols(m_buttons_sizer->GetCols() + 1);
    m_sizer->Layout();
    return true;
}

void ButtonsListCtrl::RemovePage(size_t n)
{
    auto *btn = m_pageButtons[n];
    m_pageButtons.erase(m_pageButtons.begin() + n);
    m_buttons_sizer->Remove(n);
    btn->Reparent(nullptr);
    btn->Destroy();
    m_sizer->Layout();
}

bool ButtonsListCtrl::SetPageImage(size_t n, const std::string &bmp_name) const
{
    if (n >= m_pageButtons.size())
        return false;
    return m_pageButtons[n]->SetBitmap_(bmp_name);
}

void ButtonsListCtrl::SetPageText(size_t n, const wxString &strText)
{
    auto *btn = m_pageButtons[n];
    btn->SetLabel(strText);
}

wxString ButtonsListCtrl::GetPageText(size_t n) const
{
    auto *btn = m_pageButtons[n];
    return btn->GetLabel();
}
