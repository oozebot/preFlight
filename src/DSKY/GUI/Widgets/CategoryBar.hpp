///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include <wx/bmpbndl.h>
#include <wx/colour.h>
#include <wx/panel.h>

#include <functional>
#include <string>
#include <vector>

namespace DSKY
{

// A strip of equal cells, one active, that switches what the panel below it shows: the override
// panel's category pages (icon cells) and the sidebar's extruders (text cells with a colour
// swatch). Cells wrap to further rows when a row would hold more than its maximum (SetMaxPerRow)
// or a cell would be narrower than MIN_CELL_EM em; the rows are then filled evenly, the first
// ones holding the extra cells (13 cells in two rows: 7 and 6; in three rows: 5, 4 and 4), in
// columns aligned across the rows.
class CategoryBar : public wxPanel
{
public:
    struct Item
    {
        wxString title;        // the cell's tooltip
        std::string icon_name; // an icon cell when set
        wxBitmapBundle icon;
        wxString text;   // a text cell's label
        wxColour swatch; // drawn beside the text when valid
    };

    static constexpr double MIN_CELL_EM = 2.4;

    // `background` gives the strip's own background, read on every paint so a theme change needs
    // no call
    CategoryBar(wxWindow *parent, std::function<wxColour()> background);

    void SetItems(std::vector<Item> items);
    // Makes the cell active and reports it through the change callback
    void SetActive(int index);
    // Makes the cell active without reporting it (the owner already switched)
    void SetSelection(int index);
    int GetActive() const { return m_active; }
    void SetOnChanged(std::function<void(int)> cb) { m_on_changed = std::move(cb); }

    // One count per item, drawn beside the icon while it is above zero
    void SetCounts(std::vector<int> counts);
    // One mark per item: a dot in the modified-value colour on the cell's corner
    void SetMarks(std::vector<bool> marks);
    // One swatch per item, changed in place: the items, the active cell and the rows stay
    void SetSwatches(const std::vector<wxColour> &swatches);
    // The most cells a row holds; 0 for no limit but the width
    void SetMaxPerRow(int cells);

    // The strip takes focus: Left and Right select the neighbour, Home and End the first and last,
    // Up and Down the same column of the next row (its last cell when that row is shorter)
    void EnableKeyboard(bool enable);

    // The cells row `row` holds when `count` cells fill `rows` rows evenly
    static int RowCells(int count, int rows, int row);

    void UpdateAppearance();

    bool AcceptsFocus() const override { return m_keyboard; }
    bool AcceptsFocusFromKeyboard() const override { return m_keyboard; }

private:
    int Columns() const;
    int Rows() const;
    int RowHeight() const;
    // A cell's row and column, and back (-1 for no cell)
    void CellPosition(int index, int &row, int &column) const;
    int CellIndex(int row, int column) const;
    wxRect CellRect(int index) const;
    int HitTest(const wxPoint &pt) const;

    void OnPaint(wxPaintEvent &);
    void OnMouseDown(wxMouseEvent &evt);
    void OnMouseMove(wxMouseEvent &evt);
    void OnMouseLeave(wxMouseEvent &);
    void OnKeyDown(wxKeyEvent &evt);
    void OnSize(wxSizeEvent &evt);

    std::function<wxColour()> m_background;
    std::vector<Item> m_items;
    std::vector<int> m_counts;
    std::vector<bool> m_marks;
    int m_active{0};
    int m_hovered{-1};
    bool m_keyboard{false};
    int m_rows{1};
    int m_max_per_row{0};
    std::function<void(int)> m_on_changed;
};

} // namespace DSKY
