///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2018 - 2020 Tomáš Mészáros @tamasmeszaros, Vojtěch Bubník @bubnikv
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <string>
#include <functional>

namespace Luminary
{

/**
 * @brief Generic progress indication interface.
 */
class ProgressIndicator
{
public:
    /// Cancel callback function type
    using CancelFn = std::function<void()>;

    virtual ~ProgressIndicator() = default;

    virtual void set_range(int range) = 0;
    virtual void set_cancel_callback(CancelFn = CancelFn()) = 0;
    virtual void set_progress(int pr) = 0;
    virtual void set_status_text(const char *) = 0; // utf8 char array
    virtual int get_range() const = 0;
};

} // namespace Luminary
