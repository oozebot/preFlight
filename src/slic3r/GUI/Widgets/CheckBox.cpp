///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "CheckBox.hpp"
#include "UIColors.hpp"
#include "../GUI_App.hpp"

// DPI scaling helper for bitmap margin (text-to-checkbox spacing)
static int GetScaledBitmapMargin()
{
    return (Slic3r::GUI::wxGetApp().em_unit() * 4) / 10; // 4px at 100%
}

// Icon size - ScalableBitmap/wxBitmapBundle handles DPI scaling automatically
const int px_cnt = 16;

CheckBox::CheckBox(wxWindow *parent, const wxString &name)
    : BitmapToggleButton(parent, name, wxID_ANY)
    , m_on(this, "check_on", px_cnt)
    , m_off(this, "check_off", px_cnt)
    , m_on_disabled(this, "check_on_disabled", px_cnt)
    , m_off_disabled(this, "check_off_disabled", px_cnt)
    , m_on_focused(this, "check_on_focused", px_cnt)
    , m_off_focused(this, "check_off_focused", px_cnt)
{
#ifdef __WXOSX__ // State not fully implement on MacOS
    Bind(wxEVT_SET_FOCUS, &CheckBox::updateBitmap, this);
    Bind(wxEVT_KILL_FOCUS, &CheckBox::updateBitmap, this);
    Bind(wxEVT_ENTER_WINDOW, &CheckBox::updateBitmap, this);
    Bind(wxEVT_LEAVE_WINDOW, &CheckBox::updateBitmap, this);
#endif

    // Set initial background color: use the app's themed default to ensure
    // correct color on GTK3 where parent containers may not have dark bg set yet.
    SetBackgroundColour(Slic3r::GUI::wxGetApp().get_window_default_clr());

    bool is_dark = Slic3r::GUI::wxGetApp().dark_mode();
    SetForegroundColour(is_dark ? UIColors::PanelForegroundDark() : UIColors::PanelForegroundLight());

    update();
}

void CheckBox::SetValue(bool value)
{
    wxBitmapToggleButton::SetValue(value);
    update();
}

void CheckBox::Update()
{
    update();
}

void CheckBox::Rescale()
{
    update();
}

void CheckBox::sys_color_changed()
{
    // Reload all bitmaps for the new theme
    m_on.sys_color_changed();
    m_off.sys_color_changed();
    m_on_disabled.sys_color_changed();
    m_off_disabled.sys_color_changed();
    m_on_focused.sys_color_changed();
    m_off_focused.sys_color_changed();

    // Update background to app's themed default color
    SetBackgroundColour(Slic3r::GUI::wxGetApp().get_window_default_clr());

    bool is_dark = Slic3r::GUI::wxGetApp().dark_mode();
    SetForegroundColour(is_dark ? UIColors::PanelForegroundDark() : UIColors::PanelForegroundLight());

    // Update the displayed bitmap
    update();

    // Force repaint
    Refresh();
}

void CheckBox::update()
{
    const bool val = GetValue();
    const wxBitmapBundle &bmp = (val ? m_on : m_off).bmp();
    SetBitmap(bmp);
    SetBitmapCurrent(bmp);
    SetBitmapDisabled((val ? m_on_disabled : m_off_disabled).bmp());
#ifdef __WXMSW__
    SetBitmapFocus((val ? m_on_focused : m_off_focused).bmp());
#endif
#ifdef __WXOSX__
    wxCommandEvent e(wxEVT_UPDATE_UI);
    updateBitmap(e);
#endif

    if (GetBitmapMargins().GetWidth() == 0 && !GetLabelText().IsEmpty())
        SetBitmapMargins(GetScaledBitmapMargin(), 0);
    update_size();
}

#ifdef __WXMSW__

CheckBox::State CheckBox::GetNormalState() const
{
    return State_Normal;
}

#endif

bool CheckBox::Enable(bool enable)
{
    bool result = wxBitmapToggleButton::Enable(enable);

#ifdef __WXOSX__
    if (result)
    {
        m_disable = !enable;
        wxCommandEvent e(wxEVT_ACTIVATE);
        updateBitmap(e);
    }
#endif
    // Checkbox background should always match panel - disabled state shown by grayed icon
    // (no special disabled background needed unlike text inputs)
    return result;
}

#ifdef __WXOSX__

wxBitmap CheckBox::DoGetBitmap(State which) const
{
    if (m_disable)
    {
        return wxBitmapToggleButton::DoGetBitmap(State_Disabled);
    }
    if (m_focus)
    {
        return wxBitmapToggleButton::DoGetBitmap(State_Current);
    }
    return wxBitmapToggleButton::DoGetBitmap(which);
}

void CheckBox::updateBitmap(wxEvent &evt)
{
    evt.Skip();
    if (evt.GetEventType() == wxEVT_ENTER_WINDOW)
    {
        m_hover = true;
    }
    else if (evt.GetEventType() == wxEVT_LEAVE_WINDOW)
    {
        m_hover = false;
    }
    else
    {
        if (evt.GetEventType() == wxEVT_SET_FOCUS)
        {
            m_focus = true;
        }
        else if (evt.GetEventType() == wxEVT_KILL_FOCUS)
        {
            m_focus = false;
        }
        wxMouseEvent e;
        if (m_hover)
            OnEnterWindow(e);
        else
            OnLeaveWindow(e);
    }
}

#endif
