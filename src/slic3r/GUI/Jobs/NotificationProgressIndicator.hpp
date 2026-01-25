///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2021 Tomáš Mészáros @tamasmeszaros
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef NOTIFICATIONPROGRESSINDICATOR_HPP
#define NOTIFICATIONPROGRESSINDICATOR_HPP

#include "ProgressIndicator.hpp"

namespace Slic3r
{
namespace GUI
{

class NotificationManager;

class NotificationProgressIndicator : public ProgressIndicator
{
    NotificationManager *m_nm = nullptr;
    CancelFn m_cancelfn;

public:
    explicit NotificationProgressIndicator(NotificationManager *nm);

    void set_range(int range) override;
    void set_cancel_callback(CancelFn = CancelFn()) override;
    void set_progress(int pr) override;
    void set_status_text(const char *) override; // utf8 char array
    int get_range() const override;
};

} // namespace GUI
} // namespace Slic3r

#endif // NOTIFICATIONPROGRESSINDICATOR_HPP
