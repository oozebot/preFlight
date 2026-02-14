///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "TextInput.hpp"
#include "ScrollBar.hpp"
#include "UIColors.hpp"

#include <wx/dcgraph.h>
#include <wx/panel.h>

#include "slic3r/GUI/GUI_App.hpp"

// DPI scaling helper functions
static int GetScaledSmallPadding()
{
    return (Slic3r::GUI::wxGetApp().em_unit() * 4) / 10; // 4px at 100% DPI
}

static int GetScaledSmallMargin()
{
    return Slic3r::GUI::wxGetApp().em_unit() / 2; // 5px at 100% DPI
}

static int GetScaledHeightPadding()
{
    return (Slic3r::GUI::wxGetApp().em_unit() * 8) / 10; // 8px at 100% DPI
}

#ifdef _WIN32
#include <windows.h>
#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")
#endif

BEGIN_EVENT_TABLE(TextInput, wxPanel)

EVT_PAINT(TextInput::paintEvent)

END_EVENT_TABLE()

/*
 * Called by the system of by wxWidgets when the panel needs
 * to be redrawn. You can also trigger this call by
 * calling Refresh()/Update().
 */

TextInput::TextInput()
    : label_color(std::make_pair(0x909090, (int) StateColor::Disabled),
                  std::make_pair(0x6B6B6B, (int) StateColor::Normal))
    , text_color(std::make_pair(0x909090, (int) StateColor::Disabled),
                 std::make_pair(0x262E30, (int) StateColor::Normal))
{
    if (Slic3r::GUI::wxGetApp().suppress_round_corners())
        radius = 0;
    border_width = 1;
}

TextInput::~TextInput()
{
#ifdef _WIN32
    if (m_hEditBgBrush != NULL)
    {
        DeleteObject(m_hEditBgBrush);
        m_hEditBgBrush = NULL;
    }
#endif
}

TextInput::TextInput(wxWindow *parent, wxString text, wxString label, wxString icon, const wxPoint &pos,
                     const wxSize &size, long style)
    : TextInput()
{
    Create(parent, text, label, icon, pos, size, style);
}

void TextInput::Create(wxWindow *parent, wxString text, wxString label, wxString icon, const wxPoint &pos,
                       const wxSize &size, long style)
{
    text_ctrl = nullptr;
    StaticBox::Create(parent, wxID_ANY, pos, size, style);
    wxWindow::SetLabel(label);

    state_handler.attach({&label_color, &text_color});
    state_handler.update_binds();
    const int small_padding = GetScaledSmallPadding();
    text_ctrl = new Slic3r::GUI::ThemedTextCtrl(this, wxID_ANY, text, {small_padding, small_padding}, size,
                                                style | wxBORDER_NONE);
#ifdef __WXOSX__
    text_ctrl->OSXDisableAllSmartSubstitutions();
#endif // __WXOSX__
#ifdef _WIN32
    if (style & wxTE_MULTILINE)
    {
        // preFlight: hide native scrollbar and use custom themed ScrollBar.
        // Remove WS_VSCROLL to prevent native scrollbar from appearing.
        // The EDIT control still supports scrolling via EM_LINESCROLL and mouse wheel.
        HWND hwnd = (HWND) text_ctrl->GetHWND();
        LONG ws = GetWindowLong(hwnd, GWL_STYLE);
        ws &= ~(WS_VSCROLL | WS_HSCROLL);
        SetWindowLong(hwnd, GWL_STYLE, ws);
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

        // Disable visual styles so custom bg colors work via WM_CTLCOLOREDIT
        SetWindowTheme(hwnd, L"", L"");

        m_scrollbar = new ScrollBar(this);

        // Scrollbar drag → scroll the text control
        m_scrollbar->Bind(wxEVT_SCROLL_THUMBTRACK,
                          [this](wxScrollEvent &e)
                          {
                              if (!text_ctrl)
                                  return;
                              HWND h = (HWND) text_ctrl->GetHWND();
                              int cur = (int) SendMessage(h, EM_GETFIRSTVISIBLELINE, 0, 0);
                              int target = e.GetPosition();
                              SendMessage(h, EM_LINESCROLL, 0, target - cur);
                          });

        // Sync scrollbar whenever text content or scroll position changes
        text_ctrl->Bind(wxEVT_TEXT,
                        [this](wxCommandEvent &e)
                        {
                            e.Skip();
                            CallAfter([this]() { SyncScrollbar(); });
                        });
        text_ctrl->Bind(wxEVT_SIZE,
                        [this](wxSizeEvent &e)
                        {
                            e.Skip();
                            CallAfter([this]() { SyncScrollbar(); });
                        });
        text_ctrl->Bind(wxEVT_SCROLLWIN_THUMBTRACK,
                        [this](wxScrollWinEvent &e)
                        {
                            e.Skip();
                            CallAfter([this]() { SyncScrollbar(); });
                        });
        text_ctrl->Bind(wxEVT_SCROLLWIN_THUMBRELEASE,
                        [this](wxScrollWinEvent &e)
                        {
                            e.Skip();
                            CallAfter([this]() { SyncScrollbar(); });
                        });
        text_ctrl->Bind(wxEVT_SCROLLWIN_LINEDOWN,
                        [this](wxScrollWinEvent &e)
                        {
                            e.Skip();
                            CallAfter([this]() { SyncScrollbar(); });
                        });
        text_ctrl->Bind(wxEVT_SCROLLWIN_LINEUP,
                        [this](wxScrollWinEvent &e)
                        {
                            e.Skip();
                            CallAfter([this]() { SyncScrollbar(); });
                        });
        text_ctrl->Bind(wxEVT_SCROLLWIN_PAGEDOWN,
                        [this](wxScrollWinEvent &e)
                        {
                            e.Skip();
                            CallAfter([this]() { SyncScrollbar(); });
                        });
        text_ctrl->Bind(wxEVT_SCROLLWIN_PAGEUP,
                        [this](wxScrollWinEvent &e)
                        {
                            e.Skip();
                            CallAfter([this]() { SyncScrollbar(); });
                        });
        text_ctrl->Bind(wxEVT_MOUSEWHEEL,
                        [this](wxMouseEvent &e)
                        {
                            e.Skip();
                            CallAfter([this]() { SyncScrollbar(); });
                        });
        text_ctrl->Bind(wxEVT_KEY_DOWN,
                        [this](wxKeyEvent &e)
                        {
                            e.Skip();
                            CallAfter([this]() { SyncScrollbar(); });
                        });
    }
    else
    {
        // Single-line: disable visual styles for custom background color support
        SetWindowTheme((HWND) text_ctrl->GetHWND(), L"", L"");
    }
#endif
    text_ctrl->SetInitialSize(text_ctrl->GetBestSize());
    // Use UIColors to ensure correct theme colors on startup
    bool is_dark = Slic3r::GUI::wxGetApp().dark_mode();
    wxColour bg_color = is_dark ? UIColors::InputBackgroundDark() : UIColors::InputBackgroundLight();
    wxColour fg_color = is_dark ? UIColors::InputForegroundDark() : UIColors::InputForegroundLight();
    SetBackgroundColour(bg_color);
    SetForegroundColour(fg_color);
    // Set themed colors on the internal text control - these will persist through theme changes
    text_ctrl->SetThemedColors(bg_color, fg_color);
    state_handler.attach_child(text_ctrl);

    text_ctrl->Bind(wxEVT_KILL_FOCUS,
                    [this](auto &e)
                    {
                        OnEdit();
                        e.SetId(GetId());
                        ProcessEventLocally(e);
                        e.Skip();
                    });
    text_ctrl->Bind(wxEVT_TEXT_ENTER,
                    [this](auto &e)
                    {
                        OnEdit();
                        e.SetId(GetId());
                        ProcessEventLocally(e);
                    });
    text_ctrl->Bind(wxEVT_TEXT,
                    [this](auto &e)
                    {
                        e.SetId(GetId());
                        ProcessEventLocally(e);
                    });
    text_ctrl->Bind(wxEVT_RIGHT_DOWN, [](auto &e) {}); // disable context menu

    if (!icon.IsEmpty())
    {
        this->drop_down_icon = ScalableBitmap(this, icon.ToStdString(), 16);
        this->Bind(wxEVT_LEFT_DOWN,
                   [this](wxMouseEvent &event)
                   {
                       const wxPoint pos = event.GetLogicalPosition(wxClientDC(this));
                       if (OnClickDropDownIcon && dd_icon_rect.Contains(pos))
                           OnClickDropDownIcon();
                       event.Skip();
                   });
    }
    messureSize();
}

void TextInput::SetLabel(const wxString &label)
{
    wxWindow::SetLabel(label);
    messureSize();
    Refresh();
}

bool TextInput::SetBackgroundColour(const wxColour &colour)
{
    // Use UIColors for disabled background - NOT legacy constants!
    bool is_dark = Slic3r::GUI::wxGetApp().dark_mode();
    wxColour disabled_bg = is_dark ? UIColors::InputBackgroundDisabledDark() : UIColors::InputBackgroundDisabledLight();
    const StateColor clr_state(std::make_pair(disabled_bg, (int) StateColor::Disabled),
                               std::make_pair(clr_background_focused, (int) StateColor::Checked),
                               std::make_pair(colour, (int) StateColor::Focused),
                               std::make_pair(colour, (int) StateColor::Normal));

    SetBackgroundColor(clr_state);
    if (text_ctrl)
        text_ctrl->SetBackgroundColour(colour);

    return true;
}

bool TextInput::SetForegroundColour(const wxColour &colour)
{
    // Use UIColors for disabled foreground - NOT legacy constants!
    bool is_dark = Slic3r::GUI::wxGetApp().dark_mode();
    wxColour disabled_fg = is_dark ? UIColors::InputForegroundDisabledDark() : UIColors::InputForegroundDisabledLight();
    const StateColor clr_state(std::make_pair(disabled_fg, (int) StateColor::Disabled),
                               std::make_pair(colour, (int) StateColor::Normal));

    SetLabelColor(clr_state);
    SetTextColor(clr_state);

    // Actually apply the color to text_ctrl!
    if (text_ctrl)
        text_ctrl->SetForegroundColour(colour);

    return true;
}

void TextInput::SetValue(const wxString &value)
{
    if (text_ctrl)
        text_ctrl->SetValue(value);
}

wxString TextInput::GetValue()
{
    if (text_ctrl)
        return text_ctrl->GetValue();
    return wxEmptyString;
}

void TextInput::SetSelection(long from, long to)
{
    if (text_ctrl)
        text_ctrl->SetSelection(from, to);
}

void TextInput::SysColorsChanged()
{
    bool is_dark = Slic3r::GUI::wxGetApp().dark_mode();

    // Use UIColors namespace for centralized color management
    wxColour bg_normal = is_dark ? UIColors::InputBackgroundDark() : UIColors::InputBackgroundLight();
    wxColour fg_normal = is_dark ? UIColors::InputForegroundDark() : UIColors::InputForegroundLight();

#ifdef _WIN32
    // Invalidate the cached brush so it gets recreated with new color on next WM_CTLCOLOR
    if (m_hEditBgBrush != NULL)
    {
        DeleteObject(m_hEditBgBrush);
        m_hEditBgBrush = NULL;
    }
#endif

    // Set wxWindow background (needed for proper rendering)
    SetBackgroundColour(bg_normal);
    SetForegroundColour(fg_normal);

    // Apply themed colors to internal text control
    if (text_ctrl)
    {
        text_ctrl->SetThemedColors(bg_normal, fg_normal);
#ifdef _WIN32
        // Single-line and multiline edit controls both have visual styles disabled
        // so WM_CTLCOLOREDIT brush returns work. Just force a repaint.
        RedrawWindow((HWND) text_ctrl->GetHWND(), NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
#endif
    }

    // preFlight: refresh custom scrollbar on theme change
    if (m_scrollbar)
        m_scrollbar->sys_color_changed();

    if (this->drop_down_icon.bmp().IsOk())
        this->drop_down_icon.sys_color_changed();
}

void TextInput::SetIcon(const wxBitmapBundle &icon_in)
{
    icon = icon_in;
}

void TextInput::SetLabelColor(StateColor const &color)
{
    label_color = color;
    state_handler.update_binds();
}

void TextInput::SetTextColor(StateColor const &color)
{
    text_color = color;
    state_handler.update_binds();
    if (text_ctrl)
        text_ctrl->SetForegroundColour(text_color.colorForStates(state_handler.states()));
}

void TextInput::SetBGColor(StateColor const &color)
{
    background_color = color;
    state_handler.update_binds();
}

void TextInput::SetCtrlSize(wxSize const &size)
{
    StaticBox::SetInitialSize(size);
    Rescale();
}

void TextInput::Rescale()
{
    if (text_ctrl)
        text_ctrl->SetInitialSize(text_ctrl->GetBestSize());

    messureSize();
    Refresh();
}

void TextInput::SyncScrollbar()
{
#ifdef _WIN32
    if (!m_scrollbar || !text_ctrl || !text_ctrl->IsMultiLine())
        return;

    HWND hwnd = (HWND) text_ctrl->GetHWND();

    // Get scroll metrics from the EDIT control
    int total_lines = (int) SendMessage(hwnd, EM_GETLINECOUNT, 0, 0);
    int first_visible = (int) SendMessage(hwnd, EM_GETFIRSTVISIBLELINE, 0, 0);

    // Estimate visible lines from control height and font height
    wxClientDC dc(text_ctrl);
    dc.SetFont(text_ctrl->GetFont());
    int line_h = dc.GetCharHeight();
    int visible_lines = line_h > 0 ? (text_ctrl->GetClientSize().y / line_h) : 1;
    if (visible_lines < 1)
        visible_lines = 1;

    m_scrollbar->SetScrollbar(first_visible, visible_lines, total_lines, visible_lines);

    // Only show scrollbar when content exceeds visible area
    bool needs_scroll = total_lines > visible_lines;
    if (needs_scroll != m_scrollbar->IsShown())
    {
        m_scrollbar->Show(needs_scroll);
        // Re-layout to reclaim/give space for the scrollbar
        wxSize size = GetSize();
        DoSetSize(GetPosition().x, GetPosition().y, size.x, size.y, 0);
    }
#endif
}

bool TextInput::SetFont(const wxFont &font)
{
    bool ret = StaticBox::SetFont(font);
    if (text_ctrl)
        return ret && text_ctrl->SetFont(font);
    return ret;
}

bool TextInput::Enable(bool enable)
{
    // On Windows, disabled native edit controls ignore SetBackgroundColour and use system colors.
    // ThemedTextCtrl handles this via WM_CTLCOLOREDIT interception.
    // We still make it read-only to prevent input while "disabled".
#ifdef _WIN32
    // Use IsThisEnabled() (own state only) rather than IsEnabled() (checks parent chain).
    // IsEnabled() returns false when a parent is disabled, which would cause EVT_ENABLE_CHANGED
    // to fire spuriously, toggling the StateHandler's Enabled bit and leaving controls looking
    // disabled even after the parent is re-enabled.
    bool changed = wxWindowBase::IsThisEnabled() != enable;
    wxWindow::Enable(enable);

    if (changed && text_ctrl)
    {
        text_ctrl->SetEditable(enable);

        bool dark = Slic3r::GUI::wxGetApp().dark_mode();
        wxColour bg_color, fg_color;
        if (enable)
        {
            bg_color = dark ? UIColors::InputBackgroundDark() : UIColors::InputBackgroundLight();
            fg_color = dark ? UIColors::InputForegroundDark() : UIColors::InputForegroundLight();
        }
        else
        {
            bg_color = dark ? UIColors::InputBackgroundDisabledDark() : UIColors::InputBackgroundDisabledLight();
            fg_color = dark ? UIColors::InputForegroundDisabledDark() : UIColors::InputForegroundDisabledLight();
        }

        // Invalidate cached brush so it gets recreated with new colors
        if (m_hEditBgBrush != NULL)
        {
            DeleteObject(m_hEditBgBrush);
            m_hEditBgBrush = NULL;
        }

        // Use ThemedTextCtrl's themed colors for reliable Windows color handling
        text_ctrl->SetThemedColors(bg_color, fg_color);

        // Send EVT_ENABLE_CHANGED FIRST so state_handler updates before refresh
        wxCommandEvent e(EVT_ENABLE_CHANGED);
        e.SetEventObject(this);
        GetEventHandler()->ProcessEvent(e);

        // Force the edit control to repaint - RedrawWindow triggers WM_CTLCOLOREDIT
        RedrawWindow((HWND) text_ctrl->GetHWND(), NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
        Refresh();
    }
    return changed;
#else
    bool result = text_ctrl->Enable(enable) && wxWindow::Enable(enable);
    if (result)
    {
        wxCommandEvent e(EVT_ENABLE_CHANGED);
        e.SetEventObject(this);
        GetEventHandler()->ProcessEvent(e);
        text_ctrl->SetBackgroundColour(background_color.colorForStates(state_handler.states()));
        text_ctrl->SetForegroundColour(text_color.colorForStates(state_handler.states()));
    }
    return result;
#endif
}

void TextInput::SetMinSize(const wxSize &size)
{
    wxSize size2 = size;
    if (size2.y < 0)
    {
#ifdef __WXMAC__
        if (GetPeer()) // peer is not ready in Create on mac
#endif
            size2.y = GetSize().y;
    }
    wxWindow::SetMinSize(size2);
}

void TextInput::DoSetSize(int x, int y, int width, int height, int sizeFlags)
{
    wxWindow::DoSetSize(x, y, width, height, sizeFlags);
    if (sizeFlags & wxSIZE_USE_EXISTING)
        return;
    wxSize size = GetSize();
    const int small_margin = GetScaledSmallMargin();
    wxPoint textPos = {small_margin, 0};
    if (this->icon.IsOk())
    {
        wxSize szIcon = get_preferred_size(icon, m_parent);
        textPos.x += szIcon.x;
    }
    wxSize dd_icon_size = wxSize(0, 0);
    if (this->drop_down_icon.bmp().IsOk())
        dd_icon_size = this->drop_down_icon.GetSize();

    bool align_right = GetWindowStyle() & wxRIGHT;
    if (align_right)
        textPos.x += labelSize.x;
    if (text_ctrl)
    {
        wxSize textSize = text_ctrl->GetBestSize();
        if (textSize.y > size.y)
        {
            // Don't allow to set internal control height more, then its initial height
            textSize.y = text_ctrl->GetSize().y;
        }
        wxClientDC dc(this);
        const int r_shift = int(dd_icon_size.x == 0 ? (3. * dc.GetContentScaleFactor())
                                                    : ((size.y - dd_icon_size.y) / 2));
        // preFlight: reserve space for custom scrollbar on multiline controls (only when visible)
        int scrollbar_w = (m_scrollbar && m_scrollbar->IsShown()) ? ScrollBar::GetScaledScrollbarWidth() : 0;
        textSize.x = size.x - textPos.x - labelSize.x - dd_icon_size.x - r_shift - scrollbar_w;
        if (textSize.x < -1)
            textSize.x = -1;
        text_ctrl->SetSize(textSize);
        text_ctrl->SetPosition({textPos.x, (size.y - textSize.y) / 2});

        // Position custom scrollbar at right edge of text area
        if (m_scrollbar && m_scrollbar->IsShown())
        {
            int sb_x = textPos.x + textSize.x;
            int sb_y = (size.y - textSize.y) / 2;
            m_scrollbar->SetSize(sb_x, sb_y, scrollbar_w, textSize.y);
            SyncScrollbar();
        }
    }
}

void TextInput::DoSetToolTipText(wxString const &tip)
{
    wxWindow::DoSetToolTipText(tip);
    text_ctrl->SetToolTip(tip);
}

void TextInput::paintEvent(wxPaintEvent &evt)
{
    // depending on your system you may need to look at double-buffered dcs
    wxPaintDC dc(this);
    render(dc);
}

/*
 * Here we do the actual rendering. I put it in a separate
 * method so that it can work no matter what type of DC
 * (e.g. wxPaintDC or wxClientDC) is used.
 */
void TextInput::render(wxDC &dc)
{
    StaticBox::render(dc);
    int states = state_handler.states();
    wxSize size = GetSize();
    bool align_right = GetWindowStyle() & wxRIGHT;
    const int small_margin = GetScaledSmallMargin();
    // start draw
    wxPoint pt = {small_margin + text_ctrl->GetMargins().x, 0};
    if (icon.IsOk())
    {
        wxSize szIcon = get_preferred_size(icon, m_parent);
        pt.y = (size.y - szIcon.y) / 2;
#ifdef __WXGTK3__
        dc.DrawBitmap(icon.GetBitmap(szIcon), pt);
#else
        dc.DrawBitmap(icon.GetBitmapFor(m_parent), pt);
#endif
        pt.x += szIcon.x + small_margin;
    }

    // drop_down_icon draw
    wxPoint pt_r = {size.x, 0};
    if (drop_down_icon.bmp().IsOk())
    {
        wxSize szIcon = drop_down_icon.GetSize();
        pt_r.y = (size.y - szIcon.y) / 2;
        pt_r.x -= szIcon.x + pt_r.y;
        dd_icon_rect = wxRect(pt_r, szIcon);
        dc.DrawBitmap(drop_down_icon.get_bitmap(), pt_r);
        pt_r.x -= GetScaledSmallMargin();
    }

    auto text = wxWindow::GetLabel();
    if (!text_ctrl->IsShown() && !text.IsEmpty())
    {
        wxSize textSize = text_ctrl->GetSize();
        if (align_right)
        {
            pt.x += textSize.x;
            pt.y = (size.y + textSize.y) / 2 - labelSize.y;
        }
        else
        {
            if (pt.x + labelSize.x > pt_r.x)
                text = wxControl::Ellipsize(text, dc, wxELLIPSIZE_END, pt_r.x - pt.x);
            pt.y = (size.y - labelSize.y) / 2;
        }
        dc.SetTextForeground(label_color.colorForStates(states));
        dc.SetFont(GetFont());
        dc.DrawText(text, pt);
    }
}

#ifdef _WIN32
WXLRESULT TextInput::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam)
{
    // Handle WM_CTLCOLOREDIT and WM_CTLCOLORSTATIC from our child ThemedTextCtrl
    // These messages are sent to the PARENT window when the edit control needs painting
    if (nMsg == WM_CTLCOLOREDIT || nMsg == WM_CTLCOLORSTATIC)
    {
        // Get colors based on current theme AND enabled state
        bool is_dark = Slic3r::GUI::wxGetApp().dark_mode();
        bool is_enabled = IsEnabled();
        wxColour bgColor, fgColor;
        if (is_enabled)
        {
            bgColor = is_dark ? UIColors::InputBackgroundDark() : UIColors::InputBackgroundLight();
            fgColor = is_dark ? UIColors::InputForegroundDark() : UIColors::InputForegroundLight();
        }
        else
        {
            bgColor = is_dark ? UIColors::InputBackgroundDisabledDark() : UIColors::InputBackgroundDisabledLight();
            fgColor = is_dark ? UIColors::InputForegroundDisabledDark() : UIColors::InputForegroundDisabledLight();
        }

        HDC hdc = (HDC) wParam;
        ::SetBkColor(hdc, RGB(bgColor.Red(), bgColor.Green(), bgColor.Blue()));
        ::SetTextColor(hdc, RGB(fgColor.Red(), fgColor.Green(), fgColor.Blue()));
        ::SetBkMode(hdc, OPAQUE);

        // Create native GDI brush directly
        if (m_hEditBgBrush != NULL)
        {
            LOGBRUSH lb;
            if (GetObject(m_hEditBgBrush, sizeof(lb), &lb) > 0)
            {
                COLORREF newColor = RGB(bgColor.Red(), bgColor.Green(), bgColor.Blue());
                if (lb.lbColor != newColor)
                {
                    DeleteObject(m_hEditBgBrush);
                    m_hEditBgBrush = NULL;
                }
            }
        }
        if (m_hEditBgBrush == NULL)
        {
            m_hEditBgBrush = CreateSolidBrush(RGB(bgColor.Red(), bgColor.Green(), bgColor.Blue()));
        }
        return (WXLRESULT) m_hEditBgBrush;
    }
    return wxNavigationEnabled<StaticBox>::MSWWindowProc(nMsg, wParam, lParam);
}
#endif

void TextInput::messureSize()
{
    wxSize size = GetSize();
    wxClientDC dc(this);
    labelSize = dc.GetTextExtent(wxWindow::GetLabel());

    const wxSize textSize = text_ctrl->GetSize();
    const wxSize iconSize = drop_down_icon.bmp().IsOk() ? drop_down_icon.GetSize() : wxSize(0, 0);
    const int height_padding = GetScaledHeightPadding();
    size.y = ((textSize.y > iconSize.y) ? textSize.y : iconSize.y) + height_padding;

    wxSize minSize = size;
    minSize.x = GetMinWidth();
    SetMinSize(minSize);
    SetSize(size);
}
