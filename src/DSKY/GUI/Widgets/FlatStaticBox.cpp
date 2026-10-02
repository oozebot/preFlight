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
#include "GdiCache.hpp"
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
void FlatStaticBox::GetBordersForSizer(int *borderTop, int *borderOther) const
{
    const wxFont font = GetFont();
    const wxString label = GetLabel();
    const double scale = GetDPIScaleFactor();
    if (m_borderTop < 0 || scale != m_borderScale || !(font == m_borderFont) || label != m_borderLabel)
    {
        wxStaticBox::GetBordersForSizer(&m_borderTop, &m_borderOther);
        m_borderFont = font;
        m_borderLabel = label;
        m_borderScale = scale;
    }
    *borderTop = m_borderTop;
    *borderOther = m_borderOther;
}

namespace
{
// The flat frame's geometry, from the control's font and label: the top line half a label height
// down, the gap the label leaves in it, the width of the bands the native frame is erased from
struct FrameMetrics
{
    bool has_label{false};
    int text_width{0};
    int top_line_y{0};
    int label_start_x{0};
    int label_end_x{0};
    int erase_width{0};
    int label_gap{0};
    int border_width{0};
};

FrameMetrics frame_metrics(HWND hwnd, HDC hdc)
{
    wchar_t label[256] = {0};
    ::GetWindowTextW(hwnd, label, 256);
    HFONT font = (HFONT)::SendMessage(hwnd, WM_GETFONT, 0, 0);
    if (!font)
        font = (HFONT)::GetStockObject(DEFAULT_GUI_FONT);
    HFONT old_font = (HFONT)::SelectObject(hdc, font);
    SIZE text = {0, 0};
    if (label[0] != 0)
        ::GetTextExtentPoint32W(hdc, label, (int) wcslen(label), &text);
    ::SelectObject(hdc, old_font);

    FrameMetrics m;
    m.has_label = label[0] != 0;
    m.text_width = text.cx;
    m.top_line_y = text.cy / 2;
    m.label_start_x = GetScaledLabelStartPadding();
    m.label_end_x = m.label_start_x + text.cx + GetScaledLabelEndPadding();
    m.erase_width = GetScaledEraseWidth();
    m.label_gap = GetScaledLabelGap();
    m.border_width = GetScaledBorderWidth();
    return m;
}

// Paints the bands the native frame is drawn in with `band` (the parent's colour), then the flat
// border over them, gapped for the label
void draw_flat_frame(HDC hdc, int width, int height, const FrameMetrics &m, HBRUSH band, HBRUSH border)
{
    RECT rc;

    // The band extends erase_width above the top line too (not just the border width), so the
    // native frame's corner pixels that sit slightly above the line don't leave a stray dot at the
    // border tips.
    rc = {0, m.top_line_y - m.erase_width, m.erase_width, height};
    ::FillRect(hdc, &rc, band);
    rc = {0, height - m.erase_width, width, height};
    ::FillRect(hdc, &rc, band);
    rc = {width - m.erase_width, m.top_line_y - m.erase_width, width, height};
    ::FillRect(hdc, &rc, band);

    if (m.has_label)
    {
        // Up to label_start_x (not label_start_x - label_gap), so the native frame's border segment
        // that resumes inside the wider label gap is covered on the left too. The label glyphs (or
        // the overlaid header panel) start at label_start_x, so nothing visible is erased. The border
        // is redrawn afterward to label_start_x - label_gap.
        rc = {0, m.top_line_y - m.erase_width, m.label_start_x, m.top_line_y + m.erase_width};
        ::FillRect(hdc, &rc, band);
        // From just past the label text (not label_end_x), so the native frame's border segment that
        // resumes inside the wider label gap is covered too. The border is redrawn afterward from
        // label_end_x + label_gap.
        rc = {m.label_start_x + m.text_width + m.label_gap, m.top_line_y - m.erase_width, width,
              m.top_line_y + m.erase_width};
        ::FillRect(hdc, &rc, band);
    }
    else
    {
        rc = {0, m.top_line_y - m.erase_width, width, m.top_line_y + m.erase_width};
        ::FillRect(hdc, &rc, band);
    }

    rc = {0, m.top_line_y, m.border_width, height};
    ::FillRect(hdc, &rc, border);
    rc = {0, height - m.border_width, width, height};
    ::FillRect(hdc, &rc, border);
    rc = {width - m.border_width, m.top_line_y, width, height};
    ::FillRect(hdc, &rc, border);

    if (m.has_label)
    {
        rc = {0, m.top_line_y, m.label_start_x - m.label_gap, m.top_line_y + m.border_width};
        ::FillRect(hdc, &rc, border);
        rc = {m.label_end_x + m.label_gap, m.top_line_y, width, m.top_line_y + m.border_width};
        ::FillRect(hdc, &rc, border);
    }
    else
    {
        rc = {0, m.top_line_y, width, m.top_line_y + m.border_width};
        ::FillRect(hdc, &rc, border);
    }
}

COLORREF colorref(const wxColour &colour)
{
    return RGB(colour.Red(), colour.Green(), colour.Blue());
}

// Leaves out of the box's paint every window over it (its rows, and the title panel on its top
// border): they paint themselves. A sibling inside the box loses WS_CLIPSIBLINGS (wx gives it to
// buttons and choices), as wx's own box paint does: the box sits above its rows in the Z-order, so
// with the style a row would clip the box's whole rectangle out of itself and paint nothing.
void exclude_windows_over(HWND box, HDC hdc)
{
    RECT box_rect;
    ::GetWindowRect(box, &box_rect);
    auto exclude = [&](HWND window, bool sibling)
    {
        RECT rect, over;
        if (window == box || !::IsWindowVisible(window) || !::GetWindowRect(window, &rect) ||
            !::IntersectRect(&over, &rect, &box_rect))
            return;
        if (sibling)
            if (const LONG_PTR style = ::GetWindowLongPtr(window, GWL_STYLE); style & WS_CLIPSIBLINGS)
            {
                ::SetWindowLongPtr(window, GWL_STYLE, style & ~LONG_PTR(WS_CLIPSIBLINGS));
                ::SetWindowPos(window, nullptr, 0, 0, 0, 0,
                               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            }
        ::ExcludeClipRect(hdc, over.left - box_rect.left, over.top - box_rect.top, over.right - box_rect.left,
                          over.bottom - box_rect.top);
    };
    for (HWND sibling = ::GetWindow(::GetParent(box), GW_CHILD); sibling != nullptr;
         sibling = ::GetWindow(sibling, GW_HWNDNEXT))
        exclude(sibling, true);
    for (HWND child = ::GetWindow(box, GW_CHILD); child != nullptr; child = ::GetWindow(child, GW_HWNDNEXT))
        exclude(child, false);
}
} // namespace

// The colour behind the box: the parent's
wxColour FlatStaticBox::BandColour() const
{
    wxWindow *parent = GetParent();
    wxColour colour = parent ? parent->GetBackgroundColour() : GetBackgroundColour();
    if (!colour.IsOk())
        colour = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
    return colour;
}

// A box whose label is blank (the sidebar's and the override panel's groups, titled by an overlay
// panel) is painted in one pass: its own colour wherever no window covers it, then the flat frame.
// wx's paint would draw the whole box into a bitmap of its size, run the native frame and label into
// it and walk every sibling window to clip them out, all under the frame drawn over it.
bool FlatStaticBox::MSWPaintBlank()
{
    HWND hwnd = (HWND) GetHWND();
    RECT window, client;
    ::GetWindowRect(hwnd, &window);
    ::GetClientRect(hwnd, &client);
    // The frame is measured in window coordinates; a box with a non-client area keeps wx's paint
    if (client.right != window.right - window.left || client.bottom != window.bottom - window.top)
        return false;

    PAINTSTRUCT ps;
    HDC hdc = ::BeginPaint(hwnd, &ps);
    if (hdc == nullptr)
    {
        ::ValidateRect(hwnd, nullptr);
        return true;
    }
    exclude_windows_over(hwnd, hdc);
    const FrameMetrics m = frame_metrics(hwnd, hdc);
    // Shared brushes: a null one (Windows refused it, counted by the cache) skips its fill
    if (HBRUSH own = GdiCache::shared_solid_brush(colorref(GetBackgroundColour())))
        ::FillRect(hdc, &client, own);
    HBRUSH band = GdiCache::shared_solid_brush(colorref(BandColour()));
    HBRUSH border = GdiCache::shared_solid_brush(colorref(m_borderColor));
    if (band != nullptr && border != nullptr)
        draw_flat_frame(hdc, client.right, client.bottom, m, band, border);
    ::EndPaint(hwnd, &ps);
    return true;
}

WXLRESULT FlatStaticBox::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam)
{
    const bool flat = nMsg == WM_PAINT && m_drawFlatBorder && m_borderColor.IsOk();
    if (flat && GetLabel().Strip(wxString::both).empty() && MSWPaintBlank())
        return 0;

    // Let Windows paint first (native frame: classic 3D in light mode, DarkMode_Explorer flat in dark mode)
    WXLRESULT result = wxStaticBox::MSWWindowProc(nMsg, wParam, lParam);

    // Paint the themed section_border over the native frame in BOTH modes, so groups match the
    // settings-tree frame (LabeledBorderPanel) instead of falling back to the native gray in dark mode.
    if (flat)
    {
        HWND hwnd = (HWND) GetHWND();
        // A window DC is not clipped to siblings, and a group's overlay header panel (title, pin
        // checkbox) is a sibling above this box on the top border. Excluding the siblings above
        // keeps the border from painting across the header, so the header never needs a refresh
        // after a box paint: refreshing it from the paint path re-invalidates the transparent box
        // underneath and the two repaint each other without end.
        HDC hdc = ::GetDCEx(hwnd, nullptr, DCX_WINDOW | DCX_CACHE | DCX_CLIPSIBLINGS);
        if (hdc == nullptr)
            return result;

        RECT window;
        ::GetWindowRect(hwnd, &window);
        const FrameMetrics m = frame_metrics(hwnd, hdc);
        HBRUSH band = GdiCache::shared_solid_brush(colorref(BandColour()));
        HBRUSH border = GdiCache::shared_solid_brush(colorref(m_borderColor));
        if (band != nullptr && border != nullptr)
            draw_flat_frame(hdc, window.right - window.left, window.bottom - window.top, m, band, border);
        ::ReleaseDC(hwnd, hdc);
    }

    return result;
}
#endif

} // namespace DSKY
