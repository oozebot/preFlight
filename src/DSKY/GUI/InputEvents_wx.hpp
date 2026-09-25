///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include "InputEvents.hpp"
#include <wx/event.h>

namespace DSKY
{

inline MouseEventType wx_mouse_event_type(const wxMouseEvent &evt)
{
    if (evt.LeftDown())
        return MouseEventType::LeftDown;
    if (evt.LeftUp())
        return MouseEventType::LeftUp;
    if (evt.LeftDClick())
        return MouseEventType::LeftDClick;
    if (evt.RightDown())
        return MouseEventType::RightDown;
    if (evt.RightUp())
        return MouseEventType::RightUp;
    if (evt.RightDClick())
        return MouseEventType::RightDClick;
    if (evt.MiddleDown())
        return MouseEventType::MiddleDown;
    if (evt.MiddleUp())
        return MouseEventType::MiddleUp;
    if (evt.MiddleDClick())
        return MouseEventType::MiddleDClick;
    if (evt.Entering())
        return MouseEventType::Enter;
    if (evt.Leaving())
        return MouseEventType::Leave;
    if (evt.GetWheelRotation() != 0)
        return MouseEventType::Wheel;
    return MouseEventType::Motion;
}

inline MouseInput mouse_from_wx(const wxMouseEvent &evt)
{
    MouseInput m;
    m.type = wx_mouse_event_type(evt);
    m.x = evt.GetX();
    m.y = evt.GetY();
    m.left_down = evt.LeftIsDown();
    m.right_down = evt.RightIsDown();
    m.middle_down = evt.MiddleIsDown();
    m.shift = evt.ShiftDown();
    m.ctrl = evt.ControlDown();
    m.alt = evt.AltDown();
    m.meta = evt.MetaDown();
    m.cmd = evt.CmdDown();
    m.wheel_rotation = evt.GetWheelRotation();
    m.wheel_delta = evt.GetWheelDelta();
    m.dragging = evt.Dragging();
    m.propagate = false;
    return m;
}

inline KeyInput key_from_wx(const wxKeyEvent &evt)
{
    KeyInput k;
    if (evt.GetEventType() == wxEVT_KEY_DOWN)
        k.type = KeyEventType::KeyDown;
    else if (evt.GetEventType() == wxEVT_KEY_UP)
        k.type = KeyEventType::KeyUp;
    else
        k.type = KeyEventType::Char;
    k.key_code = evt.GetKeyCode();
    k.unicode_key = evt.GetUnicodeKey();
    k.shift = evt.ShiftDown();
    k.ctrl = evt.ControlDown();
    k.alt = evt.AltDown();
    k.meta = evt.MetaDown();
    k.cmd = evt.CmdDown();
    k.propagate = false;
    return k;
}

// wx-specific: convert KeyInput modifiers to wx bitmask for postKeyEvent bridge
inline int key_modifiers_to_wx(const KeyInput &key)
{
    int m = 0;
    if (key.alt)
        m |= wxMOD_ALT;
    if (key.ctrl)
        m |= wxMOD_CONTROL;
    if (key.shift)
        m |= wxMOD_SHIFT;
    if (key.meta)
        m |= wxMOD_META;
    return m;
}

} // namespace DSKY

