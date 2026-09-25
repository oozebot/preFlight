///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include "ITimer.hpp"
#include <memory>

class wxTimer;

namespace DSKY
{

class Timer_wx : public ITimer
{
public:
    Timer_wx();
    ~Timer_wx() override;

    Timer_wx(const Timer_wx &) = delete;
    Timer_wx &operator=(const Timer_wx &) = delete;
    Timer_wx(Timer_wx &&) = delete;
    Timer_wx &operator=(Timer_wx &&) = delete;

    void start(int milliseconds, bool oneShot = false) override;
    void stop() override;
    bool isRunning() const override;
    int getInterval() const override;
    void setCallback(std::function<void()> callback) override;

private:
    std::unique_ptr<wxTimer> m_timer;
    std::function<void()> m_callback;
};

} // namespace DSKY

