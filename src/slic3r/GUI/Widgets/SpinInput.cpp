///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "SpinInput.hpp"
#include "Button.hpp"

#include "UIColors.hpp"

#include "../GUI_App.hpp"

#include <wx/dcgraph.h>
#include <wx/panel.h>
#include <wx/spinctrl.h>
#include <wx/valtext.h>

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

Button *SpinInputBase::create_button(ButtonId id)
{
    auto btn = new Button(this, "", id == ButtonId::btnIncrease ? "spin_inc_act" : "spin_dec_act", wxBORDER_NONE,
                          wxSize(12, 7));
    btn->SetCornerRadius(0);
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
    const int clr_background_disabled = Slic3r::GUI::wxGetApp().dark_mode() ? clr_background_disabled_dark
                                                                            : clr_background_disabled_light;
    StateColor clr_state(std::make_pair(clr_background_disabled, (int) StateColor::Disabled),
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
    const int disabled_color = Slic3r::GUI::wxGetApp().dark_mode() ? clr_foreground_disabled_dark
                                                                   : clr_foreground_disabled_light;
    StateColor clr_state(std::make_pair(disabled_color, (int) StateColor::Disabled),
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
    if (auto parent = this->GetParent())
    {
        SetBackgroundColour(parent->GetBackgroundColour());
        SetForegroundColour(parent->GetForegroundColour());
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
    button_dec->Rescale();
    messureSize();
}

bool SpinInputBase::Enable(bool enable)
{
    // On Windows, disabled native edit controls ignore SetBackgroundColour and use system colors.
    // Instead of disabling the text_ctrl, we make it read-only and style it to look disabled.
#ifdef _WIN32
    bool changed = IsEnabled() != enable;
    wxWindow::Enable(enable);

    if (changed && text_ctrl)
    {
        text_ctrl->SetEditable(enable);

        bool dark = Slic3r::GUI::wxGetApp().dark_mode();
        if (enable)
        {
            text_ctrl->SetBackgroundColour(wxColour(dark ? 0x2B2B2B : 0xFFFFFF));
            text_ctrl->SetForegroundColour(dark ? wxColour(0xFA, 0xFA, 0xFA) : wxColour(0x26, 0x2E, 0x30));
        }
        else
        {
            text_ctrl->SetBackgroundColour(wxColour(dark ? 0x404040 : 0xD9D9D9));
            text_ctrl->SetForegroundColour(wxColour(dark ? 0x808080 : 0x909090));
        }
        text_ctrl->Refresh();

        button_inc->Enable(enable);
        button_dec->Enable(enable);

        wxCommandEvent e(EVT_ENABLE_CHANGED);
        e.SetEventObject(this);
        GetEventHandler()->ProcessEvent(e);
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
    int h = textSize.y + 8;
    if (size.y != h)
    {
        size.y = h;
        SetSize(size);
        SetMinSize(size);
    }

    wxSize btnSize = {14, (size.y - 4) / 2};
    btnSize.x = btnSize.x * btnSize.y / 10;

    const double scale = this->GetContentScaleFactor();

    wxClientDC dc(this);
    labelSize = dc.GetMultiLineTextExtent(GetLabel());
    textSize.x = size.x - labelSize.x - btnSize.x - 16;
    text_ctrl->SetSize(textSize);
    text_ctrl->SetPosition({int(3. * scale), (size.y - textSize.y) / 2});
    button_inc->SetSize(btnSize);
    button_dec->SetSize(btnSize);
    button_inc->SetPosition({size.x - btnSize.x - int(3. * scale), size.y / 2 - btnSize.y /* - 1*/});
    button_dec->SetPosition({size.x - btnSize.x - int(3. * scale), size.y / 2 + 1});
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

    text_ctrl = new wxTextCtrl(this, wxID_ANY, text, {20, 4}, wxDefaultSize, style | wxBORDER_NONE | wxTE_PROCESS_ENTER,
                               wxTextValidator(wxFILTER_NUMERIC));
#ifdef __WXOSX__
    text_ctrl->OSXDisableAllSmartSubstitutions();
#endif // __WXOSX__
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
    if (parent)
    {
        SetBackgroundColour(parent->GetBackgroundColour());
        SetForegroundColour(parent->GetForegroundColour());
    }

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
    auto delta = ((event.GetWheelRotation() < 0) == event.IsWheelInverted()) ? step : -step;
    SetValue(val + delta);
    sendSpinEvent();
    text_ctrl->SetFocus();
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

    text_ctrl = new wxTextCtrl(this, wxID_ANY, text, {20, 4}, wxDefaultSize, style | wxBORDER_NONE | wxTE_PROCESS_ENTER,
                               wxTextValidator(wxFILTER_NUMERIC));
#ifdef __WXOSX__
    text_ctrl->OSXDisableAllSmartSubstitutions();
#endif // __WXOSX__
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
    if (parent)
    {
        SetBackgroundColour(parent->GetBackgroundColour());
        SetForegroundColour(parent->GetForegroundColour());
    }

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
    auto delta = ((event.GetWheelRotation() < 0) == event.IsWheelInverted()) ? inc : -inc;
    SetValue(val + delta);
    sendSpinEvent();
    text_ctrl->SetFocus();
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
