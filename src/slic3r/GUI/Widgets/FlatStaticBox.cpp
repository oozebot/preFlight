///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "FlatStaticBox.hpp"
#include "../GUI_App.hpp"
#include "UIColors.hpp"

#ifdef _WIN32
#include "../DarkMode.hpp"
#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")
#elif defined(__WXGTK__)
#include <gtk/gtk.h>
#endif

namespace Slic3r
{
namespace GUI
{

// DPI scaling helper shared by Windows and GTK
static int GetScaledBorderWidth()
{
    return std::max(1, wxGetApp().em_unit() / 10); // 1px at 100%, min 1px
}

#ifdef _WIN32
// DPI scaling helpers used only by Windows MSWWindowProc
static int GetScaledLabelStartPadding()
{
    return (wxGetApp().em_unit() * 8) / 10; // 8px at 100%
}

static int GetScaledLabelEndPadding()
{
    return (wxGetApp().em_unit() * 4) / 10; // 4px at 100%
}

static int GetScaledLabelGap()
{
    return std::max(1, (wxGetApp().em_unit() * 2) / 10); // 2px at 100%, min 1px
}

static int GetScaledEraseWidth()
{
    return wxGetApp().em_unit() / 3; // 3px at 100%
}
#endif

// ---------------------------------------------------------------------------
// GTK3: "draw" signal callback — connected BEFORE the default GtkFrame handler.
// We draw everything ourselves (background, border) then propagate to children
// and return TRUE to suppress the default GtkFrame decoration.
// This mirrors how LabeledBorderPanel works (full owner-draw).
// ---------------------------------------------------------------------------
#ifdef __WXGTK__

// Callback for gtk_container_forall — propagates draw to each child widget
static void propagate_draw_to_child(GtkWidget *child, gpointer data)
{
    auto *cr = static_cast<cairo_t *>(data);
    GtkWidget *parent = gtk_widget_get_parent(child);
    if (parent && GTK_IS_CONTAINER(parent))
        gtk_container_propagate_draw(GTK_CONTAINER(parent), child, cr);
}

static gboolean flatstaticbox_on_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
    auto *self = static_cast<FlatStaticBox *>(user_data);
    if (!self || !gtk_widget_get_mapped(widget))
        return FALSE;

    int w = gtk_widget_get_allocated_width(widget);
    int h = gtk_widget_get_allocated_height(widget);
    if (w <= 0 || h <= 0)
        return FALSE;

    // Border starts at textH/2 from top (same as LabeledBorderPanel)
    PangoLayout *layout = gtk_widget_create_pango_layout(widget, " ");
    wxFont wxfont = self->GetFont();
    PangoFontDescription *desc = nullptr;
    if (wxfont.IsOk())
    {
        desc = pango_font_description_from_string(static_cast<const char *>(wxfont.GetNativeFontInfoDesc().utf8_str()));
        pango_layout_set_font_description(layout, desc);
    }
    int textW, textH;
    pango_layout_get_pixel_size(layout, &textW, &textH);
    if (desc)
        pango_font_description_free(desc);
    g_object_unref(layout);

    int borderY = textH / 2;
    int borderW = GetScaledBorderWidth();

    // Colors
    wxWindow *parentWin = self->GetParent();
    wxColour bgColor = parentWin ? parentWin->GetBackgroundColour() : self->GetBackgroundColour();
    if (!bgColor.IsOk())
        bgColor = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
    wxColour sectionBg = self->GetBackgroundColour();
    wxColour borderColor = self->GetBorderColor();

    // Step 1: Fill entire widget with parent background
    cairo_set_source_rgb(cr, bgColor.Red() / 255.0, bgColor.Green() / 255.0, bgColor.Blue() / 255.0);
    cairo_paint(cr);

    // Step 2: Fill section interior
    if (sectionBg.IsOk())
    {
        cairo_set_source_rgb(cr, sectionBg.Red() / 255.0, sectionBg.Green() / 255.0, sectionBg.Blue() / 255.0);
        cairo_rectangle(cr, borderW, borderY + borderW, w - 2 * borderW, h - borderY - 2 * borderW);
        cairo_fill(cr);
    }

    // Step 3: Measure label text for top border gap (if no header panel draws it)
    wxString labelStr = self->GetLabel();
    bool hasLabel = !labelStr.IsEmpty() && labelStr.Trim() != "";
    int labelX = 0, labelEndX = 0;

    // Use bold font for label measurement/drawing (matches LabeledBorderPanel)
    PangoLayout *labelLayout = nullptr;
    PangoFontDescription *labelDesc = nullptr;
    int labelTextW = 0, labelTextH = 0;
    if (hasLabel && !self->GetHeaderPanel())
    {
        labelLayout = gtk_widget_create_pango_layout(widget, nullptr);
        wxFont boldFont = self->GetFont();
        boldFont.SetWeight(wxFONTWEIGHT_BOLD);
        labelDesc = pango_font_description_from_string(
            static_cast<const char *>(boldFont.GetNativeFontInfoDesc().utf8_str()));
        pango_layout_set_font_description(labelLayout, labelDesc);
        pango_layout_set_text(labelLayout, static_cast<const char *>(labelStr.utf8_str()), -1);
        pango_layout_get_pixel_size(labelLayout, &labelTextW, &labelTextH);

        int labelIndent = (wxGetApp().em_unit() * 8) / 10; // 8px at 100%
        int labelPad = (wxGetApp().em_unit() * 4) / 10;    // 4px padding each side
        labelX = labelIndent;
        labelEndX = labelX + labelPad + labelTextW + labelPad;
    }

    // Step 4: Draw flat border (with gap for label if no header panel)
    if (self->GetDrawFlatBorder() && borderColor.IsOk())
    {
        cairo_set_source_rgb(cr, borderColor.Red() / 255.0, borderColor.Green() / 255.0, borderColor.Blue() / 255.0);
        cairo_rectangle(cr, 0, borderY, borderW, h - borderY); // Left
        cairo_fill(cr);
        cairo_rectangle(cr, 0, h - borderW, w, borderW); // Bottom
        cairo_fill(cr);
        cairo_rectangle(cr, w - borderW, borderY, borderW, h - borderY); // Right
        cairo_fill(cr);

        // Top border — with gap for label text (no header panel case)
        if (hasLabel && !self->GetHeaderPanel() && labelEndX > 0)
        {
            cairo_rectangle(cr, 0, borderY, labelX, borderW); // Left segment
            cairo_fill(cr);
            cairo_rectangle(cr, labelEndX, borderY, w - labelEndX, borderW); // Right segment
            cairo_fill(cr);
        }
        else
        {
            cairo_rectangle(cr, 0, borderY, w, borderW); // Full top line
            cairo_fill(cr);
        }
    }

    // Step 4b: Draw label text directly (sidebar case — no header panel)
    if (labelLayout)
    {
        int labelPad = (wxGetApp().em_unit() * 4) / 10;
        wxColour fgColor = self->GetForegroundColour();
        if (!fgColor.IsOk())
            fgColor = *wxWHITE;
        cairo_set_source_rgb(cr, fgColor.Red() / 255.0, fgColor.Green() / 255.0, fgColor.Blue() / 255.0);
        cairo_move_to(cr, labelX + labelPad, 0);
        pango_cairo_show_layout(cr, labelLayout);

        pango_font_description_free(labelDesc);
        g_object_unref(labelLayout);
    }

    // Step 4: Propagate drawing to all children
    if (GTK_IS_CONTAINER(widget))
        gtk_container_forall(GTK_CONTAINER(widget), propagate_draw_to_child, cr);

    // Step 5: Redraw the header panel unclipped.
    // gtk_container_propagate_draw clips children to their GTK allocation,
    // which GtkFrame sets incorrectly for the header panel. Redraw it
    // manually using gtk_widget_draw (which does NOT clip to allocation).
    wxWindow *headerPanel = self->GetHeaderPanel();
    if (headerPanel && headerPanel->IsShownOnScreen())
    {
        GtkWidget *panelGtk = static_cast<GtkWidget *>(headerPanel->GetHandle());
        if (panelGtk && gtk_widget_get_visible(panelGtk))
        {
            wxPoint pos = headerPanel->GetPosition();
            wxSize sz = headerPanel->GetSize();
            cairo_save(cr);
            cairo_translate(cr, pos.x, pos.y);
            cairo_rectangle(cr, 0, 0, sz.x, sz.y);
            cairo_clip(cr);
            gtk_widget_draw(panelGtk, cr);
            cairo_restore(cr);
        }
    }

    // Step 6: Re-draw left/right/bottom border edges AFTER children
    if (self->GetDrawFlatBorder() && borderColor.IsOk())
    {
        cairo_set_source_rgb(cr, borderColor.Red() / 255.0, borderColor.Green() / 255.0, borderColor.Blue() / 255.0);
        cairo_rectangle(cr, 0, borderY, borderW, h - borderY); // Left
        cairo_fill(cr);
        cairo_rectangle(cr, 0, h - borderW, w, borderW); // Bottom
        cairo_fill(cr);
        cairo_rectangle(cr, w - borderW, borderY, borderW, h - borderY); // Right
        cairo_fill(cr);
    }

    return TRUE;
}
#endif // __WXGTK__

// ---------------------------------------------------------------------------

FlatStaticBox::FlatStaticBox(wxWindow *parent, wxWindowID id, const wxString &label, const wxPoint &pos,
                             const wxSize &size, long style, const wxString &name)
{
    Create(parent, id, label, pos, size, style, name);
}

bool FlatStaticBox::Create(wxWindow *parent, wxWindowID id, const wxString &label, const wxPoint &pos,
                           const wxSize &size, long style, const wxString &name)
{
    if (!wxStaticBox::Create(parent, id, label, pos, size, style, name))
        return false;

    UpdateTheme();

#ifdef __WXGTK__
    // Hook the GtkFrame's "draw" signal BEFORE the default class handler.
    // We draw everything ourselves and return TRUE to suppress GtkFrame's
    // native decoration.  Block any existing wxWidgets draw handler first
    // (wxBG_STYLE_PAINT installs one that returns TRUE, stopping emission).
    GtkWidget *gtkWidget = static_cast<GtkWidget *>(GetHandle());
    if (gtkWidget)
    {
        // Remove the GtkFrame's label widget — we draw everything ourselves.
        // This eliminates the GtkFrame's internal top padding for the label,
        // so the content area starts near the top of the widget (like a plain panel).
        if (GTK_IS_FRAME(gtkWidget))
            gtk_frame_set_label(GTK_FRAME(gtkWidget), nullptr);

        // Block any existing draw handlers (wxWidgets may connect one that returns TRUE)
        guint sig_id = g_signal_lookup("draw", G_OBJECT_TYPE(gtkWidget));
        gulong existing;
        while ((existing = g_signal_handler_find(gtkWidget,
                                                 (GSignalMatchType) (G_SIGNAL_MATCH_ID | G_SIGNAL_MATCH_UNBLOCKED),
                                                 sig_id, 0, nullptr, nullptr, nullptr)) != 0)
            g_signal_handler_block(gtkWidget, existing);

        // Connect our handler BEFORE the default class handler
        g_signal_connect(gtkWidget, "draw", G_CALLBACK(flatstaticbox_on_draw), this);
    }
#endif

    return true;
}

void FlatStaticBox::UpdateTheme()
{
#ifdef _WIN32
    if (wxGetApp().dark_mode())
    {
        // Dark mode: use DarkMode_Explorer theme which has built-in flat borders
        NppDarkMode::SetDarkExplorerTheme(GetHWND());
        // Set lighter background for section interiors (#161B22 vs page #0D1117)
        SetBackgroundColour(UIColors::InputBackgroundDark());
        SetForegroundColour(UIColors::InputForegroundDark());
    }
    else
    {
        // Light mode: use classic theme for correct border POSITION (50% label height)
        // We'll paint flat colors over the 3D effect in WM_PAINT
        SetWindowTheme((HWND) GetHWND(), L"", L"");
        SetBackgroundColour(UIColors::InputBackgroundLight());
        SetForegroundColour(UIColors::InputForegroundLight());
    }
#elif defined(__WXGTK__)
    // GTK3: set colors and border color; the draw callback renders everything.
    if (wxGetApp().dark_mode())
    {
        SetBackgroundColour(UIColors::InputBackgroundDark());
        SetForegroundColour(UIColors::InputForegroundDark());
        m_borderColor = wxColour(255, 255, 255); // white — matches LabeledBorderPanel
    }
    else
    {
        SetBackgroundColour(UIColors::InputBackgroundLight());
        SetForegroundColour(UIColors::InputForegroundLight());
        // m_borderColor default (0,0,0) is correct for light mode
    }
#endif
}

void FlatStaticBox::SysColorsChanged()
{
    UpdateTheme();
    Refresh();
}

#ifdef _WIN32
WXLRESULT FlatStaticBox::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam)
{
    // Let Windows paint first (this draws the 3D border in light mode with classic theme)
    WXLRESULT result = wxStaticBox::MSWWindowProc(nMsg, wParam, lParam);

    // Only flatten the border in light mode
    // Dark mode uses DarkMode_Explorer theme which already has flat borders
    bool is_dark = wxGetApp().dark_mode();

    if (nMsg == WM_PAINT && m_drawFlatBorder && m_borderColor.IsOk() && !is_dark)
    {
        HWND hwnd = (HWND) GetHWND();
        HDC hdc = ::GetWindowDC(hwnd);

        // Get window dimensions
        RECT windowRect;
        ::GetWindowRect(hwnd, &windowRect);
        int width = windowRect.right - windowRect.left;
        int height = windowRect.bottom - windowRect.top;

        // Get label text and calculate its extent
        wchar_t labelText[256] = {0};
        ::GetWindowTextW(hwnd, labelText, 256);

        // Get the font used by the control
        HFONT hFont = (HFONT)::SendMessage(hwnd, WM_GETFONT, 0, 0);
        if (!hFont)
            hFont = (HFONT)::GetStockObject(DEFAULT_GUI_FONT);
        HFONT oldFont = (HFONT)::SelectObject(hdc, hFont);

        SIZE textSize = {0, 0};
        if (labelText[0] != 0)
            ::GetTextExtentPoint32W(hdc, labelText, (int) wcslen(labelText), &textSize);

        ::SelectObject(hdc, oldFont);

        int topLineY = textSize.cy / 2;
        int labelStartX = GetScaledLabelStartPadding();
        int labelEndX = labelStartX + textSize.cx + GetScaledLabelEndPadding();
        int eraseWidth = GetScaledEraseWidth();
        int labelGap = GetScaledLabelGap();
        int borderW = GetScaledBorderWidth();

        // Get background color from the control's parent via wxWidgets
        wxWindow *wxParent = GetParent();
        wxColour wxBgColor = wxParent ? wxParent->GetBackgroundColour() : GetBackgroundColour();
        if (!wxBgColor.IsOk())
            wxBgColor = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
        COLORREF bgColor = RGB(wxBgColor.Red(), wxBgColor.Green(), wxBgColor.Blue());

        HBRUSH bgBrush = ::CreateSolidBrush(bgColor);
        HBRUSH borderBrush = ::CreateSolidBrush(RGB(m_borderColor.Red(), m_borderColor.Green(), m_borderColor.Blue()));

        RECT rc;

        // Erase the 3D border by painting background color over it
        rc = {0, topLineY - borderW, eraseWidth, height};
        ::FillRect(hdc, &rc, bgBrush);
        rc = {0, height - eraseWidth, width, height};
        ::FillRect(hdc, &rc, bgBrush);
        rc = {width - eraseWidth, topLineY - borderW, width, height};
        ::FillRect(hdc, &rc, bgBrush);

        if (labelText[0] != 0)
        {
            rc = {0, topLineY - borderW, labelStartX - labelGap, topLineY + eraseWidth};
            ::FillRect(hdc, &rc, bgBrush);
            rc = {labelEndX + labelGap, topLineY - borderW, width, topLineY + eraseWidth};
            ::FillRect(hdc, &rc, bgBrush);
        }
        else
        {
            rc = {0, topLineY - borderW, width, topLineY + eraseWidth};
            ::FillRect(hdc, &rc, bgBrush);
        }

        // Draw flat border
        rc = {0, topLineY, borderW, height};
        ::FillRect(hdc, &rc, borderBrush);
        rc = {0, height - borderW, width, height};
        ::FillRect(hdc, &rc, borderBrush);
        rc = {width - borderW, topLineY, width, height};
        ::FillRect(hdc, &rc, borderBrush);

        if (labelText[0] != 0)
        {
            rc = {0, topLineY, labelStartX - labelGap, topLineY + borderW};
            ::FillRect(hdc, &rc, borderBrush);
            rc = {labelEndX + labelGap, topLineY, width, topLineY + borderW};
            ::FillRect(hdc, &rc, borderBrush);
        }
        else
        {
            rc = {0, topLineY, width, topLineY + borderW};
            ::FillRect(hdc, &rc, borderBrush);
        }

        ::DeleteObject(bgBrush);
        ::DeleteObject(borderBrush);
        ::ReleaseDC(hwnd, hdc);
    }

    return result;
}
#endif

} // namespace GUI
} // namespace Slic3r
