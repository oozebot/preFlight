///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2021 Tomáš Mészáros @tamasmeszaros
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "NotificationProgressIndicator.hpp"
#include "DSKY/GUI/NotificationManager.hpp"

namespace DSKY
{
using namespace Luminary;

NotificationProgressIndicator::NotificationProgressIndicator(NotificationManager *nm) : m_nm{nm} {}

void NotificationProgressIndicator::set_range(int range)
{
    m_nm->progress_indicator_set_range(range);
}

void NotificationProgressIndicator::set_cancel_callback(CancelFn fn)
{
    m_cancelfn = std::move(fn);
    m_nm->progress_indicator_set_cancel_callback(m_cancelfn);
}

void NotificationProgressIndicator::set_progress(int pr)
{
    if (!pr)
        set_cancel_callback(m_cancelfn);

    m_nm->progress_indicator_set_progress(pr);
}

void NotificationProgressIndicator::set_status_text(const char *msg)
{
    m_nm->progress_indicator_set_status_text(msg);
}

int NotificationProgressIndicator::get_range() const
{
    return m_nm->progress_indicator_get_range();
}

} // namespace DSKY
