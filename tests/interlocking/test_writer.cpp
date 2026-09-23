// Verify preview against REAL Writer output, not a duplicate flow formula.
#include "libslic3r/GCode/GCodeWriter.hpp"
#include "libslic3r/GCode/InterlockingFlow.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

int main()
{
    using namespace Slic3r;
    unsigned checks = 0;
    auto check = [&](bool condition) {
        ++checks;
        if (!condition) throw std::runtime_error("Writer check failed: " + std::to_string(checks));
    };
    auto e_word = [](const std::string &gcode) {
        auto position = gcode.find(" E");
        if (position == std::string::npos) throw std::runtime_error("Missing emitted E");
        return std::stod(gcode.substr(position + 2));
    };
    for (bool relative : {false, true})
        for (bool volumetric : {false, true}) {
            PrintConfig config;
            config.use_relative_e_distances.value = relative;
            config.use_volumetric_e.value = volumetric;
            config.gcode_flavor.value = gcfKlipper;
            config.filament_diameter.values = {1.75, 2.85};
            GCodeWriter writer;
            writer.apply_print_config(config);
            writer.set_extruders({0, 1});
            writer.set_extruder(0);
            writer.reset_e(true);
            double previous_word = 0.;
            double x = 0.;
            for (double de : {0., .000004, .000004, .000006, .000014, .05, -.02, .01, .2}) {
                const double preview = writer.preview_extrusion_volume(de);
                const double actual_word = e_word(writer.extrude_to_xy({++x, 0.}, de));
                const double actual_delta = actual_word - (relative ? 0. : previous_word);
                const double factor = volumetric ? 1. : writer.extruder()->filament_crossection();
                check(std::abs(preview - actual_delta * factor) < 1e-12);
                previous_word = actual_word;
            }
            // Explicit retract and unretract must also advance emitted-E state.
            for (bool retract : {true, false}) {
                const std::string output = retract ? writer.retract() : writer.unretract();
                if (output.find(" E") != std::string::npos)
                    previous_word = e_word(output);
                const double preview = writer.preview_extrusion_volume(.03);
                const double word = e_word(writer.extrude_to_xy({++x, 0.}, .03));
                const double factor = volumetric ? 1. : writer.extruder()->filament_crossection();
                check(std::abs(preview - (word - (relative ? 0. : previous_word))*factor) < 1e-12);
                previous_word = word;
            }
            writer.update_extrusion_position(0, 0.);
            previous_word = 0.;
            for (int kind = 0; kind < 3; ++kind) {
                const double preview = writer.preview_extrusion_volume(.1);
                std::string output = kind == 0 ? writer.extrude_to_xyz({++x, 1., .2}, .1) :
                    writer.extrude_to_xy_G2G3IJ({++x, 1.}, {.5, 0.}, kind == 1, .1, "");
                const double word = e_word(output);
                const double factor = volumetric ? 1. : writer.extruder()->filament_crossection();
                check(std::abs(preview - (word - (relative ? 0. : previous_word)) * factor) < 1e-12);
                previous_word = word;
            }
            writer.set_extruder(1); // actual G92 in absolute mode
            const double volume = writer.preview_extrusion_volume(.1);
            const double actual = e_word(writer.extrude_to_xy({++x, 1.}, .1));
            check(std::abs(volume - actual * (volumetric ? 1. : writer.extruder()->filament_crossection())) < 1e-12);
        }
    // Exercise the guard with real quantized Writer output for explicit XY
    // deposition that rounds to its start, and for a very short XY move.
    // These are native writer/guard checks, not full-generator coverage claims.
    for (bool relative : {false, true}) {
        PrintConfig config;
        config.use_relative_e_distances.value = relative;
        config.gcode_flavor.value = gcfKlipper;
        GCodeWriter writer;
        writer.apply_print_config(config);
        writer.set_extruders({0});
        writer.set_extruder(0);
        writer.reset_e(true);
        double previous_word = 0.;
        for (double length : {0., .001}) {
            const double volume = writer.preview_extrusion_volume(.01);
            const double area = writer.extruder()->filament_crossection();
            const double motion = interlocking_motion_length(length, volume, area);
            const double feed = interlocking_feedrate(8400, 8, motion, volume);
            const std::string speed = writer.set_speed(feed, "", "");
            check(speed.find("F" + std::to_string(static_cast<int>(feed))) != std::string::npos);
            const double actual_word = e_word(writer.extrude_to_xy({length, 0.}, .01));
            const double delta = actual_word - (relative ? 0. : previous_word);
            check(std::abs(volume - delta * area) < 1e-12);
            check(delta * area * (feed / 60.) / (length == 0 ? delta : length) <= 8);
            previous_word = actual_word;
        }
        bool rejected = false;
        try {
            (void)interlocking_feedrate(8400, .0001, .001, writer.preview_extrusion_volume(.1));
        } catch (const std::invalid_argument &error) {
            rejected = std::string(error.what()) == "Interlocking volumetric limit requires a feedrate below F1";
        }
        check(rejected);
    }
    std::cout << checks << " Writer checks passed\n";
}
