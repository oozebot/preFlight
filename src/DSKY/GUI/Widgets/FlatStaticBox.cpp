///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "FlatStaticBox.hpp"
#include "../GUI_App.hpp"
#include "UIColors.hpp"
#include <wx/dcbuffer.h>

#ifdef _WIN32
#include "../DarkMode.hpp"
#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")
#elif defined(__WXGTK__)
#include <gtk/gtk.h>
#elif defined(__WXOSX__)
#include "../../Utils/MacDarkMode.hpp"
#endif

namespace DSKY
{
using namespace Luminary;

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
// GTK3: "draw" signal callback, connected ahead of the default GtkFrame handler.
// We draw everything ourselves (background, border) then propagate to children
// and return TRUE to suppress the default GtkFrame decoration.
// This mirrors how LabeledBorderPanel works (full owner-draw).
// ---------------------------------------------------------------------------
#ifdef __WXGTK__

// Callback for gtk_container_forall, propagates draw to each child widget
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

        // Top border, with gap for label text (no header panel case)
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

    // Step 4b: Draw label text directly (sidebar case, no header panel)
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
        // Remove the GtkFrame's label widget; this class draws everything itself.
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
#elif defined(__WXOSX__)
    // Make the native NSBox invisible; OnPaintMac() draws everything,
    // as GTK does via Cairo.
    mac_set_staticbox_transparent(GetHandle());
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &FlatStaticBox::OnPaintMac, this);
#endif

    return true;
}

void FlatStaticBox::UpdateTheme()
{
#ifdef _WIN32
    if (wxGetApp().dark_mode())
    {
        // Dark mode: keep DarkMode_Explorer for the interior, but paint the group border ourselves
        // (WM_PAINT) with the themed section_border so groups match the settings-tree frame.
        NppDarkMode::SetDarkExplorerTheme(GetHWND());
        // Set lighter background for section interiors (#161B22 vs page #0D1117)
        SetBackgroundColour(UIColors::InputBackgroundDark());
        SetForegroundColour(UIColors::InputForegroundDark());
        m_borderColor = UIColors::SectionBorderDark();
    }
    else
    {
        // Light mode: use classic theme for correct border POSITION (50% label height)
        // We'll paint flat colors over the 3D effect in WM_PAINT
        SetWindowTheme((HWND) GetHWND(), L"", L"");
        SetBackgroundColour(UIColors::InputBackgroundLight());
        SetForegroundColour(UIColors::InputForegroundLight());
        m_borderColor = UIColors::SectionBorderLight();
    }
#elif defined(__WXGTK__)
    // GTK3: set colors and border color; the draw callback renders everything.
    if (wxGetApp().dark_mode())
    {
        SetBackgroundColour(UIColors::InputBackgroundDark());
        SetForegroundColour(UIColors::InputForegroundDark());
        m_borderColor = UIColors::SectionBorderDark();
    }
    else
    {
        SetBackgroundColour(UIColors::InputBackgroundLight());
        SetForegroundColour(UIColors::InputForegroundLight());
        m_borderColor = UIColors::SectionBorderLight();
    }
#elif defined(__WXOSX__)
    // macOS: the NSBox is made transparent in Create().
    // All visual rendering is done by OnPaintMac().
    if (wxGetApp().dark_mode())
    {
        m_borderColor = UIColors::SectionBorderDark();
        SetForegroundColour(UIColors::LabelDefaultDark());
    }
    else
    {
        m_borderColor = UIColors::SectionBorderLight();
        SetForegroundColour(UIColors::InputForegroundLight());
    }
#endif
}

void FlatStaticBox::SysColorsChanged()
{
    UpdateTheme();
    Refresh();
}

#ifdef __WXOSX__
// macOS custom paint: draws border with title gap and label text,
// mirroring the GTK Cairo implementation in flatstaticbox_on_draw().
void FlatStaticBox::OnPaintMac(wxPaintEvent &evt)
{
    // Use wxAutoBufferedPaintDC for flicker-free rendering
    wxAutoBufferedPaintDC dc(this);

    // On macOS, wxPaintDC for wxStaticBox covers the client area, which may be
    // smaller than GetSize(). Use the DC's actual drawable area to ensure borders
    // at the right/bottom edges are visible.
    int w, h;
    dc.GetSize(&w, &h);
    if (w <= 0 || h <= 0)
        return;

    int borderW = std::max(1, wxGetApp().em_unit() / 10);

    // Measure text height to determine border start Y
    wxFont boldFont = GetFont();
    boldFont.SetWeight(wxFONTWEIGHT_BOLD);
    dc.SetFont(boldFont);
    int textH = dc.GetCharHeight();
    int borderY = textH / 2;

    // Step 1: Fill entire widget with parent background
    wxWindow *parentWin = GetParent();
    wxColour bgColor = parentWin ? parentWin->GetBackgroundColour() : GetBackgroundColour();
    if (!bgColor.IsOk())
        bgColor = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
    dc.SetBrush(wxBrush(bgColor));
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRectangle(0, 0, w, h);

    // Step 2: Measure label for top border gap
    wxString labelStr = GetLabel();
    bool hasLabel = !labelStr.IsEmpty();
    int labelX = 0, labelEndX = 0;
    int labelTextW = 0, labelTextH = 0;
    if (hasLabel)
    {
        dc.GetTextExtent(labelStr, &labelTextW, &labelTextH);
        int labelIndent = (wxGetApp().em_unit() * 8) / 10; // 8px at 100%
        int labelPad = (wxGetApp().em_unit() * 4) / 10;    // 4px padding each side
        labelX = labelIndent;
        labelEndX = labelX + labelPad + labelTextW + labelPad;
    }

    // Step 3: Draw flat border with gap for label
    if (m_drawFlatBorder && m_borderColor.IsOk())
    {
        dc.SetBrush(wxBrush(m_borderColor));
        dc.SetPen(*wxTRANSPARENT_PEN);

        // Left
        dc.DrawRectangle(0, borderY, borderW, h - borderY);
        // Bottom
        dc.DrawRectangle(0, h - borderW, w, borderW);
        // Right
        dc.DrawRectangle(w - borderW, borderY, borderW, h - borderY);

        // Top, with gap for label text
        if (hasLabel && labelEndX > 0)
        {
            dc.DrawRectangle(0, borderY, labelX, borderW);                // Left segment
            dc.DrawRectangle(labelEndX, borderY, w - labelEndX, borderW); // Right segment
        }
        else
        {
            dc.DrawRectangle(0, borderY, w, borderW); // Full top line
        }
    }

    // Step 4: Draw label text
    if (hasLabel)
    {
        int labelPad = (wxGetApp().em_unit() * 4) / 10;
        wxColour fgColor = GetForegroundColour();
        if (!fgColor.IsOk())
            fgColor = *wxWHITE;
        dc.SetTextForeground(fgColor);
        dc.SetFont(boldFont);
        dc.DrawText(labelStr, labelX + labelPad, 0);
    }
}
#endif // __WXOSX__

#ifdef _WIN32
WXLRESULT FlatStaticBox::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam)
{
    // Let Windows paint first (native frame: classic 3D in light mode, DarkMode_Explorer flat in dark mode)
    WXLRESULT result = wxStaticBox::MSWWindowProc(nMsg, wParam, lParam);

    // Paint the themed section_border over the native frame in BOTH modes, so groups match the
    // settings-tree frame (LabeledBorderPanel) instead of falling back to the native gray in dark mode.
    if (nMsg == WM_PAINT && m_drawFlatBorder && m_borderColor.IsOk())
    {
        HWND hwnd = (HWND) GetHWND();
        // A window DC is not clipped to siblings, and the sidebar places its overlay header panel
        // (title, pin checkbox) as a sibling above this box on the top border. Excluding the
        // siblings above keeps the border from painting across the header, so the header never
        // needs a refresh after a box paint: refreshing it from the paint path re-invalidates the
        // transparent box underneath and the two repaint each other without end.
        HDC hdc = ::GetDCEx(hwnd, nullptr, DCX_WINDOW | DCX_CACHE | DCX_CLIPSIBLINGS);
        if (hdc == nullptr)
            return result;

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

        // Erase the native border by painting background color over it. The band extends
        // eraseWidth above the top line too (not just borderW), so the native frame's corner
        // pixels that sit slightly above the line don't leave a stray dot at the border tips.
        rc = {0, topLineY - eraseWidth, eraseWidth, height};
        ::FillRect(hdc, &rc, bgBrush);
        rc = {0, height - eraseWidth, width, height};
        ::FillRect(hdc, &rc, bgBrush);
        rc = {width - eraseWidth, topLineY - eraseWidth, width, height};
        ::FillRect(hdc, &rc, bgBrush);

        if (labelText[0] != 0)
        {
            // Erase up to labelStartX (not labelStartX - labelGap), so the native frame's
            // border segment that resumes inside our wider label gap is covered on the left too.
            // The label glyphs (or the overlaid header panel) start at labelStartX, so nothing
            // visible is erased. The themed border is redrawn afterward to labelStartX - labelGap.
            rc = {0, topLineY - eraseWidth, labelStartX, topLineY + eraseWidth};
            ::FillRect(hdc, &rc, bgBrush);
            // Erase from just past the label text (not labelEndX), so the native frame's
            // border segment that resumes inside our wider label gap is covered too. The
            // themed border is redrawn afterward from labelEndX + labelGap.
            rc = {labelStartX + textSize.cx + labelGap, topLineY - eraseWidth, width, topLineY + eraseWidth};
            ::FillRect(hdc, &rc, bgBrush);
        }
        else
        {
            rc = {0, topLineY - eraseWidth, width, topLineY + eraseWidth};
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

} // namespace DSKY
