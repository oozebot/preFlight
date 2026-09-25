///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "Timer_wx.hpp"

#include <wx/timer.h>

namespace DSKY
{

class CallbackTimer : public wxTimer
{
public:
    CallbackTimer(std::function<void()> *callback_ptr) : m_callback_ptr(callback_ptr) {}
    void Notify() override
    {
        if (*m_callback_ptr)
            (*m_callback_ptr)();
    }

private:
    std::function<void()> *m_callback_ptr;
};

Timer_wx::Timer_wx() : m_timer(std::make_unique<CallbackTimer>(&m_callback)) {}

Timer_wx::~Timer_wx()
{
    m_timer->Stop();
}

void Timer_wx::start(int milliseconds, bool oneShot)
{
    m_timer->Start(milliseconds, oneShot);
}

void Timer_wx::stop()
{
    m_timer->Stop();
}

bool Timer_wx::isRunning() const
{
    return m_timer->IsRunning();
}

int Timer_wx::getInterval() const
{
    return m_timer->GetInterval();
}

void Timer_wx::setCallback(std::function<void()> callback)
{
    m_callback = std::move(callback);
}

} // namespace DSKY
