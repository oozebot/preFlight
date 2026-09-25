///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2016 - 2023 Pavel Mikuš @Godrak, Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Filip Sykala @Jony01, David Kocík @kocikdav, Roman Beránek @zavorka, Enrico Turri @enricoturri1966, Tomáš Mészáros @tamasmeszaros, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2021 Justin Schuh @jschuh
///|/ Copyright (c) Slic3r 2013 - 2015 Alessandro Ranellucci @alranel
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "luminary/core/text/Duration.hpp"

#include <cstdio>

#include "luminary/core/I18N.hpp"
#include "luminary/core/format.hpp"

namespace Luminary
{

std::string short_time(const std::string &time, bool force_localization /*= false*/)
{
    // Parse the dhms time format.
    int days = 0;
    int hours = 0;
    int minutes = 0;
    int seconds = 0;
    if (time.find('d') != std::string::npos)
        ::sscanf(time.c_str(), "%dd %dh %dm %ds", &days, &hours, &minutes, &seconds);
    else if (time.find('h') != std::string::npos)
        ::sscanf(time.c_str(), "%dh %dm %ds", &hours, &minutes, &seconds);
    else if (time.find('m') != std::string::npos)
        ::sscanf(time.c_str(), "%dm %ds", &minutes, &seconds);
    else if (time.find('s') != std::string::npos)
        ::sscanf(time.c_str(), "%ds", &seconds);
    // Round to full minutes.
    if (days + hours + minutes > 0 && seconds >= 30)
    {
        if (++minutes == 60)
        {
            minutes = 0;
            if (++hours == 24)
            {
                hours = 0;
                ++days;
            }
        }
    }

    // Format the dhm time

    if (force_localization)
    {
        auto get_d = [days]()
        {
            return format(_u8L("%1%d"), days);
        };
        auto get_h = [hours]()
        {
            return format(_u8L("%1%h"), hours);
        };
        // TRN "m" means "minutes"
        auto get_m = [minutes]()
        {
            return format(_u8L("%1%m"), minutes);
        };

        if (days > 0)
            return get_d() + get_h() + get_m();
        if (hours > 0)
            return get_h() + get_m();
        if (minutes > 0)
            return get_m();
        return format(_u8L("%1%s"), seconds);
    }

    char buffer[64];
    if (days > 0)
        ::sprintf(buffer, "%dd%dh%dm", days, hours, minutes);
    else if (hours > 0)
        ::sprintf(buffer, "%dh%dm", hours, minutes);
    else if (minutes > 0)
        ::sprintf(buffer, "%dm", minutes);
    else
        ::sprintf(buffer, "%ds", seconds);
    return buffer;
}

} // namespace Luminary
