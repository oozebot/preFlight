///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "SpinInput.hpp"
#include "Button.hpp"

#include "UIColors.hpp"

#include "../GUI_App.hpp"

// Height padding scales with DPI (matches TextInput)
static int GetScaledHeightPadding()
{
    return (Slic3r::GUI::wxGetApp().em_unit() * 8) / 10; // 8px at 100% DPI
}

// Fixed base sizes for button layout - wxBitmapBundle handles icon DPI scaling

static int GetButtonBaseWidth()
{
    return 14;
}

static int GetButtonHeightOffset()
{
    return 4;
}

static int GetTextMargin()
{
    return 16;
}

static int GetSmallOffset()
{
    return 1;
}

#include <wx/dcgraph.h>
#include <wx/panel.h>
#include <wx/spinctrl.h>
#include <wx/valtext.h>

#ifdef _WIN32
#include <windows.h>
#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")
#endif

BEGIN_EVENT_TABLE(SpinInputBase, wxPanel)

EVT_KEY_DOWN(SpinInputBase::keyPressed)
EVT_MOUSEWHEEL(SpinInputBase::mouseWheelMoved)

EVT_PAINT(SpinInputBase::paintEvent)

END_EVENT_TABLE()

/*
 * Called by the system of by wxWidgets when the panel needs
 * to be redrawn. You can also trigger this call by
 * calling Refresh()/Update().
 */

SpinInputBase::SpinInputBase()
    : label_color(std::make_pair(0x909090, (int) StateColor::Disabled),
                  std::make_pair(0x6B6B6B, (int) StateColor::Normal))
    , text_color(std::make_pair(0x909090, (int) StateColor::Disabled),
                 std::make_pair(0x262E30, (int) StateColor::Normal))
{
    if (Slic3r::GUI::wxGetApp().suppress_round_corners())
        radius = 0;
    border_width = 1;
}

SpinInputBase::~SpinInputBase()
{
#ifdef _WIN32
    if (m_hEditBgBrush != NULL)
    {
        DeleteObject(m_hEditBgBrush);
        m_hEditBgBrush = NULL;
    }
#endif
}

Button *SpinInputBase::create_button(ButtonId id)
{
    // Fixed base icon size (12x7), wxBitmapBundle handles DPI scaling
    auto btn = new Button(this, "", id == ButtonId::btnIncrease ? "spin_inc_act" : "spin_dec_act", wxBORDER_NONE,
                          wxSize(12, 7));
    btn->SetCornerRadius(0);
    btn->SetBorderWidth(0);
    btn->SetBorderColor(StateColor());
    btn->SetBackgroundColor(StateColor());
    btn->SetInactiveIcon(id == ButtonId::btnIncrease ? "spin_inc" : "spin_dec");
    btn->DisableFocusFromKeyboard();
    btn->SetSelected(false);

    bind_inc_dec_button(btn, id);

    return btn;
}

void SpinInputBase::SetCornerRadius(double radius)
{
    this->radius = radius;
    Refresh();
}

void SpinInputBase::SetLabel(const wxString &label)
{
    wxWindow::SetLabel(label);
    messureSize();
    Refresh();
}

void SpinInputBase::SetLabelColor(StateColor const &color)
{
    label_color = color;
    state_handler.update_binds();
}

void SpinInputBase::SetTextColor(StateColor const &color)
{
    text_color = color;
    state_handler.update_binds();
}

void SpinInputBase::SetSize(wxSize const &size)
{
    wxWindow::SetSize(size);
    Rescale();
}

wxString SpinInputBase::GetTextValue() const
{
    return text_ctrl->GetValue();
}

void SpinInput::SetRange(int min, int max)
{
    this->min = min;
    this->max = max;
}

void SpinInputBase::SetSelection(long from, long to)
{
    if (text_ctrl)
        text_ctrl->SetSelection(from, to);
}

bool SpinInputBase::SetFont(wxFont const &font)
{
    if (text_ctrl)
        return text_ctrl->SetFont(font);
    return StaticBox::SetFont(font);
}

bool SpinInputBase::SetBackgroundColour(const wxColour &colour)
{
    // Use UIColors for disabled background - NOT legacy constants!
    bool is_dark = Slic3r::GUI::wxGetApp().dark_mode();
    wxColour disabled_bg = is_dark ? UIColors::InputBackgroundDisabledDark() : UIColors::InputBackgroundDisabledLight();
    StateColor clr_state(std::make_pair(disabled_bg, (int) StateColor::Disabled),
                         std::make_pair(clr_background_focused, (int) StateColor::Checked),
                         std::make_pair(colour, (int) StateColor::Focused),
                         std::make_pair(colour, (int) StateColor::Normal));

    StaticBox::SetBackgroundColor(clr_state);
    if (text_ctrl)
        text_ctrl->SetBackgroundColour(colour);
    if (button_inc)
        button_inc->SetBackgroundColor(clr_state);
    if (button_dec)
        button_dec->SetBackgroundColor(clr_state);

    return true;
}

bool SpinInputBase::SetForegroundColour(const wxColour &colour)
{
    // Use UIColors for disabled foreground - NOT legacy constants!
    bool is_dark = Slic3r::GUI::wxGetApp().dark_mode();
    wxColour disabled_fg = is_dark ? UIColors::InputForegroundDisabledDark() : UIColors::InputForegroundDisabledLight();
    StateColor clr_state(std::make_pair(disabled_fg, (int) StateColor::Disabled),
                         std::make_pair(colour, (int) StateColor::Normal));

    SetLabelColor(clr_state);
    SetTextColor(clr_state);

    if (text_ctrl)
        text_ctrl->SetForegroundColour(colour);
    if (button_inc)
        button_inc->SetTextColor(clr_state);
    if (button_dec)
        button_dec->SetTextColor(clr_state);

    return true;
}

void SpinInputBase::SysColorsChanged()
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

    // Apply to internal text control using ThemedTextCtrl for reliable Windows color handling
    if (text_ctrl)
    {
        text_ctrl->SetThemedColors(bg_normal, fg_normal);
#ifdef _WIN32
        // DO NOT call SetDarkExplorerTheme on edit controls!
        // Edit controls have visual styles disabled at creation via SetWindowTheme(hwnd, L"", L"")
        // which allows WM_CTLCOLOREDIT brush returns to work. Applying DarkMode_Explorer
        // would re-enable visual styles and cause Windows to ignore our brushes.
        // Just force a repaint to apply the new colors via WM_CTLCOLOREDIT.
        RedrawWindow((HWND) text_ctrl->GetHWND(), NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
#endif
    }

    if (button_inc)
        button_inc->Rescale();
    if (button_dec)
        button_dec->Rescale();
}

void SpinInputBase::SetBorderColor(StateColor const &color)
{
    StaticBox::SetBorderColor(color);
    if (button_inc)
        button_inc->SetBorderColor(color);
    if (button_dec)
        button_dec->SetBorderColor(color);
}

void SpinInputBase::DoSetToolTipText(wxString const &tip)
{
    wxWindow::DoSetToolTipText(tip);
    text_ctrl->SetToolTip(tip);
}

void SpinInputBase::Rescale()
{
    SetFont(Slic3r::GUI::wxGetApp().normal_font());
    text_ctrl->SetInitialSize(text_ctrl->GetBestSize());

    button_inc->Rescale();
    button_inc->SetCornerRadius(0);
    button_inc->SetBorderWidth(0);
    button_dec->Rescale();
    button_dec->SetCornerRadius(0);
    button_dec->SetBorderWidth(0);
    messureSize();
}

void SpinInputBase::msw_rescale()
{
    StaticBox::msw_rescale();
    border_width = 1; // SpinInput uses fixed 1px border, not DPI-scaled
}

bool SpinInputBase::Enable(bool enable)
{
    // On Windows, disabled native edit controls ignore SetBackgroundColour and use system colors.
    // Instead of disabling the text_ctrl, we make it read-only and style it to look disabled.
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
        text_ctrl->SetThemedColors(bg_color, fg_color);

        button_inc->Enable(enable);
        button_dec->Enable(enable);

        // Send EVT_ENABLE_CHANGED FIRST so state_handler updates before refresh
        wxCommandEvent e(EVT_ENABLE_CHANGED);
        e.SetEventObject(this);
        GetEventHandler()->ProcessEvent(e);

        // Now refresh with the updated state
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
        button_inc->Enable(enable);
        button_dec->Enable(enable);
    }
    return result;
#endif
}

#ifdef _WIN32
WXLRESULT SpinInputBase::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam)
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

void SpinInputBase::paintEvent(wxPaintEvent &evt)
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
void SpinInputBase::render(wxDC &dc)
{
    StaticBox::render(dc);
    int states = state_handler.states();
    wxSize size = GetSize();
    // draw seperator of buttons
    wxPoint pt = button_inc->GetPosition();
    pt.y = size.y / 2;
    dc.SetPen(wxPen(border_color.defaultColor()));

    const double scale = dc.GetContentScaleFactor();
    const int btn_w = button_inc->GetSize().GetWidth();
    dc.DrawLine(pt, pt + wxSize{btn_w - int(scale), 0});
    // draw label
    auto label = GetLabel();
    if (!label.IsEmpty())
    {
        pt.x = size.x - labelSize.x - 5;
        pt.y = (size.y - labelSize.y) / 2;
        dc.SetFont(GetFont());
        dc.SetTextForeground(label_color.colorForStates(states));
        dc.DrawText(label, pt);
    }
}

void SpinInputBase::messureSize()
{
    wxSize size = GetSize();
    wxSize textSize = text_ctrl->GetSize();
    // Height padding scales with DPI (matches TextInput)
    const int height_padding = GetScaledHeightPadding();
    int h = textSize.y + height_padding;
    if (size.y != h)
    {
        size.y = h;
        SetSize(size);
        SetMinSize(size);
    }

    // Fixed base button sizing
    const int btn_base_width = GetButtonBaseWidth();
    const int btn_height_offset = GetButtonHeightOffset();
    const int text_margin = GetTextMargin();
    const int small_offset = GetSmallOffset();

    wxSize btnSize = {btn_base_width, (size.y - btn_height_offset) / 2};
    btnSize.x = btnSize.x * btnSize.y / 10;

    const double scale = this->GetContentScaleFactor();

    wxClientDC dc(this);
    labelSize = dc.GetMultiLineTextExtent(GetLabel());
    textSize.x = size.x - labelSize.x - btnSize.x - text_margin;
    text_ctrl->SetSize(textSize);
    text_ctrl->SetPosition({int(3. * scale), (size.y - textSize.y) / 2});
    button_inc->SetSize(btnSize);
    button_dec->SetSize(btnSize);
    button_inc->SetPosition({size.x - btnSize.x - int(3. * scale), size.y / 2 - btnSize.y});
    button_dec->SetPosition({size.x - btnSize.x - int(3. * scale), size.y / 2 + small_offset});
}

void SpinInputBase::onText(wxCommandEvent &event)
{
    sendSpinEvent();
    event.SetId(GetId());
    ProcessEventLocally(event);
}

void SpinInputBase::sendSpinEvent()
{
    wxCommandEvent event(wxEVT_SPINCTRL, GetId());
    event.SetEventObject(this);
    GetEventHandler()->ProcessEvent(event);
}

//           SpinInput

SpinInput::SpinInput(wxWindow *parent, wxString text, wxString label, const wxPoint &pos, const wxSize &size,
                     long style, int min, int max, int initial)
    : SpinInputBase()
{
    Create(parent, text, label, pos, size, style, min, max, initial);
}

void SpinInput::Create(wxWindow *parent, wxString text, wxString label, const wxPoint &pos, const wxSize &size,
                       long style, int min, int max, int initial)
{
    StaticBox::Create(parent, wxID_ANY, pos, size);
    wxWindow::SetLabel(label);

    state_handler.attach({&label_color, &text_color});
    state_handler.update_binds();

    text_ctrl = new Slic3r::GUI::ThemedTextCtrl(this, wxID_ANY, text, {20, 4}, wxDefaultSize,
                                                style | wxBORDER_NONE | wxTE_PROCESS_ENTER,
                                                wxTextValidator(wxFILTER_NUMERIC));
#ifdef __WXOSX__
    text_ctrl->OSXDisableAllSmartSubstitutions();
#endif // __WXOSX__
#ifdef _WIN32
    // Disable Windows visual styles so WM_CTLCOLOREDIT colors are respected
    SetWindowTheme((HWND) text_ctrl->GetHWND(), L"", L"");
#endif
    text_ctrl->SetInitialSize(text_ctrl->GetBestSize());
    state_handler.attach_child(text_ctrl);

    text_ctrl->Bind(wxEVT_KILL_FOCUS, &SpinInput::onTextLostFocus, this);
    text_ctrl->Bind(wxEVT_TEXT, &SpinInput::onText, this);
    text_ctrl->Bind(wxEVT_TEXT_ENTER, &SpinInput::onTextEnter, this);
    text_ctrl->Bind(wxEVT_KEY_DOWN, &SpinInput::keyPressed, this);
    text_ctrl->Bind(wxEVT_RIGHT_DOWN, [](auto &e) {}); // disable context menu
    button_inc = create_button(ButtonId::btnIncrease);
    button_dec = create_button(ButtonId::btnDecrease);
    delta = 0;
    timer.Bind(wxEVT_TIMER, &SpinInput::onTimer, this);

    SetFont(Slic3r::GUI::wxGetApp().normal_font());
    // Use UIColors to ensure correct theme colors on startup
    bool is_dark = Slic3r::GUI::wxGetApp().dark_mode();
    wxColour bg_color = is_dark ? UIColors::InputBackgroundDark() : UIColors::InputBackgroundLight();
    wxColour fg_color = is_dark ? UIColors::InputForegroundDark() : UIColors::InputForegroundLight();
    SetBackgroundColour(bg_color);
    SetForegroundColour(fg_color);
    // Set themed colors on internal text control for reliable Windows color handling
    text_ctrl->SetThemedColors(bg_color, fg_color);

    long initialFromText;
    if (text.ToLong(&initialFromText))
        initial = initialFromText;
    SetRange(min, max);
    SetValue(initial);
    messureSize();
}

void SpinInput::bind_inc_dec_button(Button *btn, ButtonId id)
{
    btn->Bind(wxEVT_LEFT_DOWN,
              [this, btn, id](auto &e)
              {
                  delta = id == ButtonId::btnIncrease ? step : -step;
                  SetValue(val + delta);
                  text_ctrl->SetFocus();
                  btn->CaptureMouse();
                  delta *= 8;
                  timer.Start(100);
                  sendSpinEvent();
              });
    btn->Bind(wxEVT_LEFT_DCLICK,
              [this, btn, id](auto &e)
              {
                  delta = id == ButtonId::btnIncrease ? step : -step;
                  btn->CaptureMouse();
                  SetValue(val + delta);
                  sendSpinEvent();
              });
    btn->Bind(wxEVT_LEFT_UP,
              [this, btn](auto &e)
              {
                  btn->ReleaseMouse();
                  timer.Stop();
                  text_ctrl->SelectAll();
                  delta = 0;
              });
}

void SpinInput::SetValue(const wxString &text)
{
    long value;
    if (text.ToLong(&value))
        SetValue(value);
    else
        text_ctrl->SetValue(text);
}

void SpinInput::SetValue(int value)
{
    if (value < min)
        value = min;
    else if (value > max)
        value = max;
    this->val = value;
    text_ctrl->SetValue(wxString::FromDouble(value));
}

int SpinInput::GetValue() const
{
    return val;
}

void SpinInput::onTimer(wxTimerEvent &evnet)
{
    if (delta < -step || delta > step)
    {
        delta /= 2;
        return;
    }
    SetValue(val + delta);
    sendSpinEvent();
}

void SpinInput::onTextLostFocus(wxEvent &event)
{
    timer.Stop();
    for (auto *child : GetChildren())
        if (auto btn = dynamic_cast<Button *>(child))
            if (btn->HasCapture())
                btn->ReleaseMouse();
    wxCommandEvent e;
    onTextEnter(e);
    // pass to outer
    event.SetId(GetId());
    ProcessEventLocally(event);
    event.Skip();
}

void SpinInput::onTextEnter(wxCommandEvent &event)
{
    long value;
    if (!text_ctrl->GetValue().ToLong(&value))
        value = val;

    if (value != val)
    {
        SetValue(value);
        sendSpinEvent();
    }
    event.SetId(GetId());
    ProcessEventLocally(event);
}

void SpinInput::mouseWheelMoved(wxMouseEvent &event)
{
    // Don't change value on mouse wheel - too easy to accidentally change settings while scrolling
    // Pass the event to parent for page scrolling
    event.Skip();
}

void SpinInput::keyPressed(wxKeyEvent &event)
{
    switch (event.GetKeyCode())
    {
    case WXK_UP:
    case WXK_DOWN:
        long value;
        if (!text_ctrl->GetValue().ToLong(&value))
        {
            value = val;
        }
        if (event.GetKeyCode() == WXK_DOWN && value - step >= min)
        {
            value -= step;
        }
        else if (event.GetKeyCode() == WXK_UP && value + step <= max)
        {
            value += step;
        }
        if (value != val)
        {
            SetValue(value);
            sendSpinEvent();
        }
        break;
    default:
        event.Skip();
        break;
    }
}

//           SpinInputDouble

SpinInputDouble::SpinInputDouble(wxWindow *parent, wxString text, wxString label, const wxPoint &pos,
                                 const wxSize &size, long style, double min, double max, double initial, double inc)
    : SpinInputBase()
{
    Create(parent, text, label, pos, size, style, min, max, initial, inc);
}

void SpinInputDouble::Create(wxWindow *parent, wxString text, wxString label, const wxPoint &pos, const wxSize &size,
                             long style, double min, double max, double initial, double inc)
{
    StaticBox::Create(parent, wxID_ANY, pos, size);
    wxWindow::SetLabel(label);

    state_handler.attach({&label_color, &text_color});
    state_handler.update_binds();

    text_ctrl = new Slic3r::GUI::ThemedTextCtrl(this, wxID_ANY, text, {20, 4}, wxDefaultSize,
                                                style | wxBORDER_NONE | wxTE_PROCESS_ENTER,
                                                wxTextValidator(wxFILTER_NUMERIC));
#ifdef __WXOSX__
    text_ctrl->OSXDisableAllSmartSubstitutions();
#endif // __WXOSX__
#ifdef _WIN32
    // Disable Windows visual styles so WM_CTLCOLOREDIT colors are respected
    SetWindowTheme((HWND) text_ctrl->GetHWND(), L"", L"");
#endif
    text_ctrl->SetInitialSize(text_ctrl->GetBestSize());
    state_handler.attach_child(text_ctrl);

    text_ctrl->Bind(wxEVT_KILL_FOCUS, &SpinInputDouble::onTextLostFocus, this);
    text_ctrl->Bind(wxEVT_TEXT, &SpinInputDouble::onText, this);
    text_ctrl->Bind(wxEVT_TEXT_ENTER, &SpinInputDouble::onTextEnter, this);
    text_ctrl->Bind(wxEVT_KEY_DOWN, &SpinInputDouble::keyPressed, this);
    text_ctrl->Bind(wxEVT_RIGHT_DOWN, [](auto &e) {}); // disable context menu
    button_inc = create_button(ButtonId::btnIncrease);
    button_dec = create_button(ButtonId::btnDecrease);
    delta = 0;
    timer.Bind(wxEVT_TIMER, &SpinInputDouble::onTimer, this);

    SetFont(Slic3r::GUI::wxGetApp().normal_font());
    // Use UIColors to ensure correct theme colors on startup
    bool is_dark = Slic3r::GUI::wxGetApp().dark_mode();
    wxColour bg_color = is_dark ? UIColors::InputBackgroundDark() : UIColors::InputBackgroundLight();
    wxColour fg_color = is_dark ? UIColors::InputForegroundDark() : UIColors::InputForegroundLight();
    SetBackgroundColour(bg_color);
    SetForegroundColour(fg_color);
    // Set themed colors on internal text control for reliable Windows color handling
    text_ctrl->SetThemedColors(bg_color, fg_color);

    double initialFromText;
    if (text.ToDouble(&initialFromText))
        initial = initialFromText;
    SetRange(min, max);
    SetIncrement(inc);
    SetValue(initial);
    messureSize();
}

void SpinInputDouble::bind_inc_dec_button(Button *btn, ButtonId id)
{
    btn->Bind(wxEVT_LEFT_DOWN,
              [this, btn, id](auto &e)
              {
                  delta = id == ButtonId::btnIncrease ? inc : -inc;
                  SetValue(val + delta);
                  text_ctrl->SetFocus();
                  btn->CaptureMouse();
                  delta *= 8;
                  timer.Start(100);
                  sendSpinEvent();
              });
    btn->Bind(wxEVT_LEFT_DCLICK,
              [this, btn, id](auto &e)
              {
                  delta = id == ButtonId::btnIncrease ? inc : -inc;
                  btn->CaptureMouse();
                  SetValue(val + delta);
                  sendSpinEvent();
              });
    btn->Bind(wxEVT_LEFT_UP,
              [this, btn](auto &e)
              {
                  btn->ReleaseMouse();
                  timer.Stop();
                  text_ctrl->SelectAll();
                  delta = 0;
              });
}

void SpinInputDouble::SetValue(const wxString &text)
{
    double value;
    if (text.ToDouble(&value))
        SetValue(value);
    else
        text_ctrl->SetValue(text);
}

void SpinInputDouble::SetValue(double value)
{
    if (Slic3r::is_approx(value, val))
        return;

    if (value < min)
        value = min;
    else if (value > max)
        value = max;
    this->val = value;
    wxString str_val = wxString::FromDouble(value, digits);
    text_ctrl->SetValue(str_val);
}

double SpinInputDouble::GetValue() const
{
    return val;
}

void SpinInputDouble::SetRange(double min, double max)
{
    this->min = min;
    this->max = max;
}

void SpinInputDouble::SetIncrement(double inc_in)
{
    inc = inc_in;
}

void SpinInputDouble::SetDigits(unsigned digits_in)
{
    digits = int(digits_in);
}

void SpinInputDouble::onTimer(wxTimerEvent &evnet)
{
    if (delta < -inc || delta > inc)
    {
        delta /= 2;
        return;
    }
    SetValue(val + delta);
    sendSpinEvent();
}

void SpinInputDouble::onTextLostFocus(wxEvent &event)
{
    timer.Stop();
    for (auto *child : GetChildren())
        if (auto btn = dynamic_cast<Button *>(child))
            if (btn->HasCapture())
                btn->ReleaseMouse();
    wxCommandEvent e;
    onTextEnter(e);
    // pass to outer
    event.SetId(GetId());
    ProcessEventLocally(event);
    event.Skip();
}

void SpinInputDouble::onTextEnter(wxCommandEvent &event)
{
    double value;
    if (!text_ctrl->GetValue().ToDouble(&value))
        val = value;

    if (!Slic3r::is_approx(value, val))
    {
        SetValue(value);
        sendSpinEvent();
    }
    event.SetId(GetId());
    ProcessEventLocally(event);
}

void SpinInputDouble::mouseWheelMoved(wxMouseEvent &event)
{
    // Don't change value on mouse wheel - too easy to accidentally change settings while scrolling
    // Pass the event to parent for page scrolling
    event.Skip();
}

void SpinInputDouble::keyPressed(wxKeyEvent &event)
{
    switch (event.GetKeyCode())
    {
    case WXK_UP:
    case WXK_DOWN:
        double value;
        if (!text_ctrl->GetValue().ToDouble(&value))
            val = value;

        if (event.GetKeyCode() == WXK_DOWN && value > min)
        {
            value -= inc;
        }
        else if (event.GetKeyCode() == WXK_UP && value + inc < max)
        {
            value += inc;
        }
        if (!Slic3r::is_approx(value, val))
        {
            SetValue(value);
            sendSpinEvent();
        }
        break;
    default:
        event.Skip();
        break;
    }
}
