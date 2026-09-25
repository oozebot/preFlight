///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2016 - 2023 Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Filip Sykala @Jony01, David Kocík @kocikdav, Enrico Turri @enricoturri1966, Tomáš Mészáros @tamasmeszaros, Lukáš Matěna @lukasmatena, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2019 Sijmen Schoon
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <cmath>
#include <cstdio>
#include <string>

namespace Luminary
{

// Shorten the dhms time by removing the seconds, rounding the dhm to full minutes
// and removing spaces.
std::string short_time(const std::string &time, bool force_localization = false);
// localized short_time used on UI
inline std::string short_time_ui(const std::string &time)
{
    return short_time(time, true);
}

// Returns the given time is seconds in format DDd HHh MMm SSs
inline std::string get_time_dhms(float time_in_secs)
{
    int days = (int) (time_in_secs / 86400.0f);
    time_in_secs -= (float) days * 86400.0f;
    int hours = (int) (time_in_secs / 3600.0f);
    time_in_secs -= (float) hours * 3600.0f;
    int minutes = (int) (time_in_secs / 60.0f);
    time_in_secs -= (float) minutes * 60.0f;

    char buffer[64];
    if (days > 0)
        ::sprintf(buffer, "%dd %dh %dm %ds", days, hours, minutes, (int) time_in_secs);
    else if (hours > 0)
        ::sprintf(buffer, "%dh %dm %ds", hours, minutes, (int) time_in_secs);
    else if (minutes > 0)
        ::sprintf(buffer, "%dm %ds", minutes, (int) time_in_secs);
    else
        ::sprintf(buffer, "%ds", (int) std::round(time_in_secs));

    return buffer;
}

inline std::string get_time_dhm(float time_in_secs)
{
    int days = (int) (time_in_secs / 86400.0f);
    time_in_secs -= (float) days * 86400.0f;
    int hours = (int) (time_in_secs / 3600.0f);
    time_in_secs -= (float) hours * 3600.0f;
    int minutes = (int) (time_in_secs / 60.0f);

    char buffer[64];
    if (days > 0)
        ::sprintf(buffer, "%dd %dh %dm", days, hours, minutes);
    else if (hours > 0)
        ::sprintf(buffer, "%dh %dm", hours, minutes);
    else if (minutes > 0)
        ::sprintf(buffer, "%dm", minutes);

    return buffer;
}

} // namespace Luminary
