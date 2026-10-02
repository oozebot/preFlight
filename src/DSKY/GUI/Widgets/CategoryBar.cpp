///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "CategoryBar.hpp"

#include "UIColors.hpp"
#include "../GUI_App.hpp"
#include "../wxExtensions.hpp"

#include <wx/dcbuffer.h>

#include <algorithm>

namespace DSKY
{

CategoryBar::CategoryBar(wxWindow *parent, std::function<wxColour()> background)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNO_BORDER), m_background(std::move(background))
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    UpdateAppearance();
    Bind(wxEVT_PAINT, &CategoryBar::OnPaint, this);
    Bind(wxEVT_LEFT_DOWN, &CategoryBar::OnMouseDown, this);
    Bind(wxEVT_MOTION, &CategoryBar::OnMouseMove, this);
    Bind(wxEVT_LEAVE_WINDOW, &CategoryBar::OnMouseLeave, this);
    Bind(wxEVT_KEY_DOWN, &CategoryBar::OnKeyDown, this);
    Bind(wxEVT_SIZE, &CategoryBar::OnSize, this);
    // The focus outline follows the keyboard focus
    for (const auto &type : {wxEVT_SET_FOCUS, wxEVT_KILL_FOCUS})
        Bind(type,
             [this](wxFocusEvent &evt)
             {
                 Refresh();
                 evt.Skip();
             });
}

void CategoryBar::SetItems(std::vector<Item> items)
{
    m_items = std::move(items);
    for (Item &item : m_items)
        if (!item.icon_name.empty())
            item.icon = *get_bmp_bundle(item.icon_name);
    // Counts and marks belong to the old items; the owner sets them again
    m_counts.clear();
    m_marks.clear();
    m_active = 0;
    m_hovered = -1;
    UpdateAppearance();
}

void CategoryBar::SetActive(int index)
{
    if (index < 0 || index >= int(m_items.size()) || index == m_active)
        return;
    m_active = index;
    Refresh();
    if (m_on_changed)
        m_on_changed(index);
}

void CategoryBar::SetSelection(int index)
{
    if (index < 0 || index >= int(m_items.size()) || index == m_active)
        return;
    m_active = index;
    Refresh();
}

void CategoryBar::SetCounts(std::vector<int> counts)
{
    m_counts = std::move(counts);
    Refresh();
}

void CategoryBar::SetMarks(std::vector<bool> marks)
{
    if (marks == m_marks)
        return;
    m_marks = std::move(marks);
    Refresh();
}

void CategoryBar::SetSwatches(const std::vector<wxColour> &swatches)
{
    bool changed = false;
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        const wxColour swatch = i < swatches.size() ? swatches[i] : wxColour();
        if (swatch != m_items[i].swatch)
        {
            m_items[i].swatch = swatch;
            changed = true;
        }
    }
    if (changed)
        Refresh();
}

void CategoryBar::SetMaxPerRow(int cells)
{
    if (cells == m_max_per_row)
        return;
    m_max_per_row = cells;
    UpdateAppearance();
}

void CategoryBar::EnableKeyboard(bool enable)
{
    m_keyboard = enable;
}

void CategoryBar::UpdateAppearance()
{
    for (Item &item : m_items)
        if (!item.icon_name.empty())
            item.icon = *get_bmp_bundle(item.icon_name);
    m_rows = Rows();
    SetMinSize(wxSize(-1, RowHeight() * m_rows));
    Refresh();
}

int CategoryBar::RowCells(int count, int rows, int row)
{
    if (rows <= 0 || row < 0 || row >= rows)
        return 0;
    return count / rows + (row < count % rows ? 1 : 0);
}

// The columns of the grid: the cells of the longest row, the first
int CategoryBar::Columns() const
{
    const int count = int(m_items.size());
    if (count == 0)
        return 1;
    const int rows = Rows();
    return (count + rows - 1) / rows;
}

// As few rows as the row maximum and the width allow
int CategoryBar::Rows() const
{
    const int count = int(m_items.size());
    if (count == 0)
        return 1;
    const int width = GetClientSize().GetWidth();
    const int min_cell = std::max(1, int(MIN_CELL_EM * wxGetApp().em_unit()));
    // Before the first layout the width is unknown and sets no limit
    int per_row = width <= 0 ? count : std::clamp(width / min_cell, 1, count);
    if (m_max_per_row > 0)
        per_row = std::min(per_row, m_max_per_row);
    return (count + per_row - 1) / per_row;
}

int CategoryBar::RowHeight() const
{
    return wxGetApp().em_unit() * 26 / 10;
}

void CategoryBar::CellPosition(int index, int &row, int &column) const
{
    const int count = int(m_items.size());
    const int rows = Rows();
    const int base = count / rows;
    row = 0;
    column = 0;
    if (base == 0)
        return;
    // The first count % rows rows hold base + 1 cells
    const int long_cells = (count % rows) * (base + 1);
    if (index < long_cells)
    {
        row = index / (base + 1);
        column = index % (base + 1);
    }
    else
    {
        row = count % rows + (index - long_cells) / base;
        column = (index - long_cells) % base;
    }
}

int CategoryBar::CellIndex(int row, int column) const
{
    const int count = int(m_items.size());
    const int rows = Rows();
    if (column < 0 || column >= RowCells(count, rows, row))
        return -1;
    int index = column;
    for (int r = 0; r < row; ++r)
        index += RowCells(count, rows, r);
    return index;
}

wxRect CategoryBar::CellRect(int index) const
{
    const wxSize size = GetClientSize();
    const int columns = Columns();
    int row = 0;
    int column = 0;
    CellPosition(index, row, column);
    const int width = size.GetWidth() / columns;
    const int x = column * width;
    const int w = (column == columns - 1) ? size.GetWidth() - x : width;
    // A single row spans the whole height, as the strip always did
    const int row_height = m_rows == 1 ? size.GetHeight() : RowHeight();
    return wxRect(x, row * row_height, w, row_height);
}

int CategoryBar::HitTest(const wxPoint &pt) const
{
    if (m_items.empty() || pt.x < 0 || pt.y < 0)
        return -1;
    for (int i = 0; i < int(m_items.size()); ++i)
        if (CellRect(i).Contains(pt))
            return i;
    return -1;
}

void CategoryBar::OnSize(wxSizeEvent &evt)
{
    // A width change can change the number of rows the cells wrap into, a DPI change the row height
    const int rows = Rows();
    if (rows != m_rows || GetMinSize().GetHeight() != RowHeight() * rows)
    {
        m_rows = rows;
        SetMinSize(wxSize(-1, RowHeight() * m_rows));
        if (GetParent() != nullptr)
            GetParent()->CallAfter([parent = GetParent()]() { parent->Layout(); });
    }
    Refresh();
    evt.Skip();
}

void CategoryBar::OnPaint(wxPaintEvent &)
{
    wxAutoBufferedPaintDC dc(this);
    const wxSize size = GetClientSize();
    const int em = wxGetApp().em_unit();
    const wxColour bg = m_background ? m_background() : GetBackgroundColour();
    dc.SetBrush(wxBrush(bg));
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRectangle(0, 0, size.GetWidth(), size.GetHeight());
    if (m_items.empty())
        return;

    const wxColour accent = UIColors::AccentPrimary();
    const wxColour raised = bg.ChangeLightness(wxGetApp().dark_mode() ? 115 : 94);
    const int count = int(m_items.size());
    const int indicator = std::max(2, em * 3 / 10);
    for (int i = 0; i < count; ++i)
    {
        const wxRect cell = CellRect(i);
        if (i == m_active || i == m_hovered)
        {
            dc.SetBrush(wxBrush(i == m_active ? raised : raised.ChangeLightness(105)));
            dc.DrawRectangle(cell);
        }
        if (i == m_active)
        {
            dc.SetBrush(wxBrush(accent));
            dc.DrawRectangle(cell.x, cell.GetBottom() + 1 - indicator, cell.width, indicator);
        }
        const int center_y = cell.y + (cell.height - indicator) / 2;
        const Item &item = m_items[i];
        if (!item.icon_name.empty())
        {
            // The icon, with the item's count beside it while it has any
            const wxBitmap icon = item.icon.GetBitmapFor(this);
            const int n = i < int(m_counts.size()) ? m_counts[i] : 0;
            const wxString count_text = n > 0 ? wxString::Format("%d", n) : wxString();
#ifdef __APPLE__
            const wxSize icon_size = icon.IsOk() ? icon.GetLogicalSize() : wxSize(0, 0);
#else
            const wxSize icon_size = icon.IsOk() ? icon.GetSize() : wxSize(0, 0);
#endif
            dc.SetFont(wxGetApp().small_font().Bold());
            dc.SetTextForeground(accent);
            const wxSize text_size = count_text.IsEmpty() ? wxSize(0, 0) : dc.GetTextExtent(count_text);
            const int gap = count_text.IsEmpty() ? 0 : em / 4;
            const int content_width = icon_size.GetWidth() + gap + text_size.GetWidth();
            const int content_x = cell.x + (cell.width - content_width) / 2;
            if (icon.IsOk())
                dc.DrawBitmap(icon, content_x, center_y - icon_size.GetHeight() / 2, true);
            if (!count_text.IsEmpty())
                dc.DrawText(count_text, content_x + icon_size.GetWidth() + gap, center_y - text_size.GetHeight() / 2);
        }
        else
        {
            // The label, with the colour swatch before it when the item has one
            dc.SetFont(i == m_active ? wxGetApp().bold_font() : wxGetApp().normal_font());
            dc.SetTextForeground(i == m_active ? accent : UIColors::PanelForeground());
            const wxSize text_size = dc.GetTextExtent(item.text);
            const int swatch = item.swatch.IsOk() ? std::max(4, em * 9 / 10) : 0;
            const int gap = swatch > 0 ? em / 4 : 0;
            const int content_x = cell.x + (cell.width - (swatch + gap + text_size.GetWidth())) / 2;
            if (swatch > 0)
            {
                dc.SetBrush(wxBrush(item.swatch));
                dc.SetPen(wxPen(UIColors::PanelForeground().ChangeLightness(wxGetApp().dark_mode() ? 60 : 140)));
                dc.DrawRectangle(content_x, center_y - swatch / 2, swatch, swatch);
                dc.SetPen(*wxTRANSPARENT_PEN);
            }
            dc.DrawText(item.text, content_x + swatch + gap, center_y - text_size.GetHeight() / 2);
        }
        // The modified mark on the cell's upper right corner
        if (i < int(m_marks.size()) && m_marks[i])
        {
            const int r = std::max(2, em * 3 / 10);
            dc.SetBrush(wxBrush(wxGetApp().get_label_clr_modified()));
            dc.DrawCircle(cell.GetRight() - r - em / 5, cell.y + r + em / 5, r);
        }
        // The keyboard focus, around the active cell
        if (m_keyboard && i == m_active && HasFocus())
        {
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.SetPen(wxPen(accent, 1, wxPENSTYLE_DOT));
            dc.DrawRectangle(wxRect(cell).Deflate(1));
            dc.SetPen(*wxTRANSPARENT_PEN);
        }
    }
    // The bar's bottom edge
    dc.SetBrush(wxBrush(raised));
    dc.DrawRectangle(0, size.GetHeight() - 1, size.GetWidth(), 1);
}

void CategoryBar::OnMouseDown(wxMouseEvent &evt)
{
    const int idx = HitTest(evt.GetPosition());
    if (m_keyboard)
        SetFocus();
    if (idx >= 0)
        SetActive(idx);
}

void CategoryBar::OnMouseMove(wxMouseEvent &evt)
{
    const int idx = HitTest(evt.GetPosition());
    if (idx != m_hovered)
    {
        m_hovered = idx;
        SetToolTip(idx >= 0 ? m_items[idx].title : wxString());
        Refresh();
    }
}

void CategoryBar::OnMouseLeave(wxMouseEvent &)
{
    if (m_hovered != -1)
    {
        m_hovered = -1;
        Refresh();
    }
}

void CategoryBar::OnKeyDown(wxKeyEvent &evt)
{
    if (!m_keyboard || m_items.empty())
    {
        evt.Skip();
        return;
    }
    switch (evt.GetKeyCode())
    {
    case WXK_LEFT:
    case WXK_NUMPAD_LEFT:
        SetActive(std::max(0, m_active - 1));
        break;
    case WXK_RIGHT:
    case WXK_NUMPAD_RIGHT:
        SetActive(std::min(int(m_items.size()) - 1, m_active + 1));
        break;
    case WXK_HOME:
    case WXK_NUMPAD_HOME:
        SetActive(0);
        break;
    case WXK_END:
    case WXK_NUMPAD_END:
        SetActive(int(m_items.size()) - 1);
        break;
    // Up and Down move between the rows, in the same column or to the last cell of a shorter row
    case WXK_UP:
    case WXK_NUMPAD_UP:
    case WXK_DOWN:
    case WXK_NUMPAD_DOWN:
    {
        const int rows = Rows();
        if (rows < 2)
        {
            evt.Skip();
            break;
        }
        const bool up = evt.GetKeyCode() == WXK_UP || evt.GetKeyCode() == WXK_NUMPAD_UP;
        int row = 0;
        int column = 0;
        CellPosition(m_active, row, column);
        const int target_row = row + (up ? -1 : 1);
        if (target_row >= 0 && target_row < rows)
        {
            const int cells = RowCells(int(m_items.size()), rows, target_row);
            SetActive(CellIndex(target_row, std::min(column, cells - 1)));
        }
        break;
    }
    default:
        evt.Skip();
        break;
    }
}

} // namespace DSKY
