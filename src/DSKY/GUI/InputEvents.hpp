///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

namespace DSKY
{

enum class MouseEventType
{
    LeftDown,
    LeftUp,
    LeftDClick,
    RightDown,
    RightUp,
    RightDClick,
    MiddleDown,
    MiddleUp,
    MiddleDClick,
    Motion,
    Enter,
    Leave,
    Wheel
};

struct MouseInput
{
    MouseEventType type;

    // Position in widget coordinates (already scaled for Retina)
    double x{0.0};
    double y{0.0};

    // Button state (which buttons are currently held down, not just this event's button)
    bool left_down{false};
    bool right_down{false};
    bool middle_down{false};

    // Modifier keys
    bool shift{false};
    bool ctrl{false};
    bool alt{false};
    bool meta{false}; // Cmd on macOS, Win on Windows
    bool cmd{false};  // Ctrl on Windows/Linux, Cmd on macOS

    // Wheel data (only valid for Wheel type)
    int wheel_rotation{0};
    int wheel_delta{120};

    // Whether this is a dragging motion (button held + moved)
    bool dragging{false};

    // Set by handler to indicate the event should propagate (replaces evt.Skip())
    mutable bool propagate{false};

    // Helper: check if any modifier is held
    bool has_any_modifiers() const { return shift || ctrl || alt || meta; }

    // Common compound checks
    bool is_any_button_up() const
    {
        return type == MouseEventType::LeftUp || type == MouseEventType::MiddleUp || type == MouseEventType::RightUp;
    }
    bool is_any_button_down() const
    {
        return type == MouseEventType::LeftDown || type == MouseEventType::MiddleDown ||
               type == MouseEventType::RightDown;
    }
};

enum class KeyEventType
{
    KeyDown,
    KeyUp,
    Char
};

struct KeyInput
{
    KeyEventType type;

    int key_code{0};
    int unicode_key{0};

    // Modifier keys
    bool shift{false};
    bool ctrl{false};
    bool alt{false};
    bool meta{false};
    bool cmd{false};

    // Set by handler to indicate the event should propagate (replaces evt.Skip())
    mutable bool propagate{false};

    // Helper: check if any modifier is held
    bool has_any_modifiers() const { return shift || ctrl || alt || meta; }
};

struct SizeInput
{
    int width{0};
    int height{0};
};

} // namespace DSKY

