///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "SwitchButton.hpp"

#include "../wxExtensions.hpp"
#include "../GUI_App.hpp"
#include "UIColors.hpp"

#include <wx/dcgraph.h>
#include <wx/dcmemory.h>
#include <wx/dcclient.h>
#include <wx/bmpbndl.h>

#include <algorithm>
#include <cmath>

// DPI scaling helpers for toggle icon
static int GetScaledToggleWidth()
{
    return (DSKY::wxGetApp().em_unit() * 28) / 10; // 28px at 100%
}

static int GetScaledToggleHeight()
{
    return (DSKY::wxGetApp().em_unit() * 16) / 10; // 16px at 100%
}

namespace
{

// Resolution-independent bundle for the labeled switch. The pill (track, thumb and both labels)
// is drawn on demand at whatever device-pixel size wx requests, so it stays crisp at any DPI
// instead of baking a single raster the button would then have to upscale. Geometry derives from
// em_unit so the pill scales with the rest of the UI; one impl is built per toggle state.
class SwitchPillBundleImpl : public wxBitmapBundleImpl
{
public:
    SwitchPillBundleImpl(bool checked, const wxString labels[2], const wxFont &font, int em, int max_width,
                         const wxColour &bg, const StateColor &track, const StateColor &thumb, const StateColor &text)
        : m_checked(checked)
        , m_font(font)
        , m_em(em)
        , m_max_width(max_width)
        , m_bg(bg)
        , m_track(track)
        , m_thumb(thumb)
        , m_text(text)
    {
        m_labels[0] = labels[0];
        m_labels[1] = labels[1];
        compute_reference();
    }

    wxSize GetDefaultSize() const override { return m_track_size; }

    wxSize GetPreferredBitmapSizeAtScale(double scale) const override
    {
        return wxSize(int(std::lround(m_track_size.x * scale)), int(std::lround(m_track_size.y * scale)));
    }

    wxBitmap GetBitmap(const wxSize &size) override
    {
        // wx must never be handed a zero/negative-sized bitmap. The padding constants in
        // compute_reference keep real requests well above zero, so this only guards a degenerate
        // call (e.g. an unrealized DC reporting a zero font extent before the deferred Rescale).
        if (size.x <= 0 || size.y <= 0)
            return wxBitmap(std::max(1, size.x), std::max(1, size.y));
        const double scale = m_track_size.y > 0 ? double(size.y) / double(m_track_size.y) : 1.0;
        wxBitmap bmp(size.x, size.y);
        render(bmp, size, scale);
        return bmp;
    }

private:
    // Pad/inset values for a given em, matching the 100%-DPI design ratios.
    static int pad_x(double em) { return int(std::lround(em * 12 / 10.0)); }            // 12px at 100%
    static int pad_y(double em) { return int(std::lround(em * 6 / 10.0)); }             // 6px at 100%
    static int spacing(double em) { return int(std::lround(em)); }                      // 10px at 100%
    static int track_pad(double em) { return int(std::lround(em * 2 / 10.0)); }         // 2px at 100%
    static int inset_of(double em) { return std::max(1, int(std::lround(em / 10.0))); } // 1px at 100%

    // Computes the 100%-equivalent track and thumb sizes (the bundle's default size). The labels
    // are measured with the base font; higher-DPI renders scale this reference proportionally.
    void compute_reference()
    {
        wxBitmap probe(1, 1);
        wxMemoryDC dc(probe);
        dc.SetFont(m_font);

        const int thumbPadX = pad_x(m_em);
        const int thumbPadY = pad_y(m_em);
        const int trackSpacing = spacing(m_em);
        const int trackPad = track_pad(m_em);

        wxSize thumb = dc.GetTextExtent(m_labels[0]);
        wxSize other = dc.GetTextExtent(m_labels[1]);
        if (other.x > thumb.x)
            thumb.x = other.x;
        else
            other.x = thumb.x;
        thumb.x += thumbPadX;
        thumb.y += thumbPadY;

        wxSize track;
        track.x = thumb.x + other.x + trackSpacing;
        track.y = thumb.y + trackPad;

        // Both pills cap to the same width so the two sidebar halves stay balanced.
        if (m_max_width > 0 && track.x > m_max_width)
        {
            thumb.x -= (track.x - m_max_width) / 2;
            track.x = m_max_width;
        }

        m_thumb_size = thumb;
        m_track_size = track;
    }

    // Non-const: StateColor::colorForStates() mutates its internal cache.
    void render(wxBitmap &bmp, const wxSize &size, double scale)
    {
        const int inset = inset_of(m_em * scale);
        const wxSize thumb(int(std::lround(m_thumb_size.x * scale)), int(std::lround(m_thumb_size.y * scale)));
        // The track fills the requested bitmap exactly, so the result needs no second scaling.
        const wxSize track = size;

        const wxFont font = m_font.Scaled((float) scale);

        wxMemoryDC memdc(bmp);
        memdc.SetBackground(wxBrush(m_bg));
        memdc.Clear();
        memdc.SetFont(font);

        // Re-measure and ellipsize each label at the rendered size so long translations degrade
        // gracefully (end ellipsis) instead of clipping mid-glyph.
        wxString drawLabels[2] = {m_labels[0], m_labels[1]};
        wxSize textSize[2] = {memdc.GetTextExtent(drawLabels[0]), memdc.GetTextExtent(drawLabels[1])};
        const int availTextW = thumb.x - 2 * inset;
        for (int i = 0; i < 2; ++i)
        {
            if (availTextW > 0 && textSize[i].x > availTextW)
            {
                drawLabels[i] = wxControl::Ellipsize(drawLabels[i], memdc, wxELLIPSIZE_END, availTextW);
                textSize[i] = memdc.GetTextExtent(drawLabels[i]);
            }
        }

        const auto state = m_checked ? (StateColor::Checked | StateColor::Enabled) : StateColor::Enabled;
        {
#ifdef __WXMSW__
            wxGCDC dc2(memdc);
#else
            wxDC &dc2(memdc);
#endif
            dc2.SetBrush(wxBrush(m_track.colorForStates(state)));
            dc2.SetPen(wxPen(m_track.colorForStates(state)));
            dc2.DrawRoundedRectangle(wxRect({0, 0}, track), track.y / 2);

            const auto thumb_clr = m_thumb.colorForStates(StateColor::Checked | StateColor::Enabled);
            dc2.SetBrush(wxBrush(thumb_clr));
            dc2.SetPen(wxPen(thumb_clr));
            const int thumb_x = m_checked ? (track.x - thumb.x - inset) : inset;
            dc2.DrawRoundedRectangle(wxRect({thumb_x, inset}, thumb), thumb.y / 2);
        }

        // The label under the thumb takes the accent-text color; the other takes the secondary color.
        memdc.SetTextForeground(m_text.colorForStates(state ^ StateColor::Checked));
        memdc.DrawText(drawLabels[0], {inset + (thumb.x - textSize[0].x) / 2, inset + (thumb.y - textSize[0].y) / 2});
        memdc.SetTextForeground(m_text.colorForStates(state));
        memdc.DrawText(drawLabels[1], {track.x - thumb.x - inset + (thumb.x - textSize[1].x) / 2,
                                       inset + (thumb.y - textSize[1].y) / 2});
        memdc.SelectObject(wxNullBitmap);
    }

    bool m_checked;
    wxString m_labels[2];
    wxFont m_font;
    int m_em;
    int m_max_width;
    wxColour m_bg;
    StateColor m_track;
    StateColor m_thumb;
    StateColor m_text;
    wxSize m_track_size;
    wxSize m_thumb_size;
};

} // namespace

SwitchButton::SwitchButton(wxWindow *parent, const wxString &name, wxWindowID id)
    : BitmapToggleButton(parent, name, id)
    , m_on(this, "toggle_on", GetScaledToggleWidth(), GetScaledToggleHeight())
    , m_off(this, "toggle_off", GetScaledToggleWidth(), GetScaledToggleHeight())
    , text_color(std::pair{UIColors::AccentText(), (int) StateColor::Checked}, // glyph on the accent thumb
                 std::pair{UIColors::SecondaryText(), (int) StateColor::Normal})
    , track_color(wxcolour_to_rgb_int(UIColors::BedGrid()))
    , thumb_color(std::pair{accent_primary_rgb(), (int) StateColor::Checked}, // themed accent
                  std::pair{wxcolour_to_rgb_int(UIColors::BedGrid()), (int) StateColor::Normal})
{
    Rescale();
}

void SwitchButton::SetLabels(wxString const &lbl_on, wxString const &lbl_off)
{
    labels[0] = lbl_on;
    labels[1] = lbl_off;
    Rescale();
}

void SwitchButton::SetTextColor(StateColor const &color)
{
    text_color = color;
}

void SwitchButton::SetTrackColor(StateColor const &color)
{
    track_color = color;
}

void SwitchButton::SetThumbColor(StateColor const &color)
{
    thumb_color = color;
}

void SwitchButton::SetValue(bool value)
{
    if (value != GetValue())
        wxBitmapToggleButton::SetValue(value);
    update();
}

void SwitchButton::Rescale()
{
    if (!labels[0].IsEmpty())
    {
        // Build a resolution-independent bundle per state; the pill is rendered crisply at whatever
        // DPI the button paints at, so there is no platform-specific bitmap scaling to manage here.
        wxClientDC dc(this);
        const wxFont font = dc.GetFont();
        const int em = DSKY::wxGetApp().em_unit();
        const int max_width = GetMaxWidth();
        const wxColour bg = GetBackgroundColour();

        m_off.SetBitmap(wxBitmapBundle::FromImpl(
            new SwitchPillBundleImpl(false, labels, font, em, max_width, bg, track_color, thumb_color, text_color)));
        m_on.SetBitmap(wxBitmapBundle::FromImpl(
            new SwitchPillBundleImpl(true, labels, font, em, max_width, bg, track_color, thumb_color, text_color)));
    }

    update();
}

void SwitchButton::SysColorChange()
{
    // Labeled pills hold custom bundles, so rebuild them (ScalableBitmap::sys_color_changed would
    // revert to the toggle_on/off SVG icons the objects were constructed with). Icon-only mode
    // refreshes its SVG bundles normally.
    if (!labels[0].IsEmpty())
    {
        Rescale();
    }
    else
    {
        m_on.sys_color_changed();
        m_off.sys_color_changed();
        update();
    }
}

void SwitchButton::update()
{
    SetBitmap((GetValue() ? m_on : m_off).bmp());
    update_size();
}
