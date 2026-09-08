// Standalone tests of the production scalar helper. Runtime checks survive Release.
#include "libslic3r/GCode/InterlockingFlow.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

int main()
{
    using Slic3r::interlocking_feedrate;
    unsigned checks = 0;
    auto check = [&](bool condition) {
        ++checks;
        if (!condition) throw std::runtime_error("Failed check " + std::to_string(checks));
    };
    auto rejects = [&](double f, double q, double length, double volume) {
        bool rejected = false;
        try { (void)interlocking_feedrate(f, q, length, volume); }
        catch (const std::invalid_argument &) { rejected = true; }
        check(rejected);
    };
    check(interlocking_feedrate(8400, 8, 2, .24) == 4000);
    check(interlocking_feedrate(1200, 8, 2, .24) == 1200);
    check(interlocking_feedrate(4000, 8, 2, .24) == 4000);
    check(interlocking_feedrate(8400, 0, 2, .24) == 8400);
    check(interlocking_feedrate(8400, 8, 2, 0) == 8400);
    check(interlocking_feedrate(8400, 8, 2, -.24) == 8400);
    check(interlocking_feedrate(8400, 8, .001, .00024) == 2000);
    check(interlocking_feedrate(8400, 8, 1, .071) == 6760);
    check(Slic3r::interlocking_motion_length(2, .24, 2.4) == 2);
    check(std::abs(Slic3r::interlocking_motion_length(0, .24, 2.4) - .1) < 1e-12);
    check(interlocking_feedrate(8400, 8, Slic3r::interlocking_motion_length(0, .24, 2.4), .24) == 200);
    // Start-to-end delta and incremental IJ, as emitted (millimetres).
    const double pi = std::acos(-1.);
    check(std::abs(Slic3r::interlocking_arc_length(2, 0, 1, 0, false) - pi) < 1e-12);
    check(std::abs(Slic3r::interlocking_arc_length(2, 0, 1, 0, true) - pi) < 1e-12);
    check(std::abs(Slic3r::interlocking_arc_length(0, 0, 1, 0, true) - 2*pi) < 1e-12);
    check(std::abs(Slic3r::interlocking_arc_length(1, -1, 1, 0, true) - pi/2) < 1e-12);
    for (double cap : {4., 8., 12.})
        for (double multiplier : {.9, 1., 1.1})
            for (double flow : {1., 1.457, 2.}) {
                double volume = .3 * multiplier * flow;
                double result = interlocking_feedrate(8400, cap, 2, volume);
                check(result <= 8400 && result == std::floor(result));
                check(volume * (result / 60.) / 2 <= cap);
                check(volume * ((result + 1) / 60.) / 2 > cap);
            }
    rejects(8400, 8, 0, .1);
    rejects(8400, 8, .000001, 1);
    rejects(0, 8, 1, .1);
    rejects(8400.5, 8, 1, .1);
    rejects(8400, -8, 1, .1);
    rejects(8400, 8, -1, .1);
    rejects(8400, 8, 1, std::numeric_limits<double>::quiet_NaN());
    rejects(8400, std::numeric_limits<double>::infinity(), 1, .1);
    std::cout << checks << " flow-limit checks passed\n";
}
