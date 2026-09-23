// Exercise the real downstream postprocessor, including its F60 floor.
#include "libslic3r/GCode.hpp"
#include "libslic3r/GCode/PressureEqualizer.hpp"
#include <iostream>
#include <stdexcept>

int main()
{
    using namespace Slic3r;
    unsigned checks = 0;
    auto check = [&](bool ok, const char *message) {
        ++checks;
        if (!ok) {
            std::cerr << "Check " << checks << ": " << message << std::endl;
            std::exit(1);
        }
    };
    for (bool relative : {false, true})
        for (unsigned tool : {0u, 1u})
          for (bool tool_in_header : {false, true})
            for (int limit_source : {0, 1, 2}) {
                PrintConfig config;
                config.use_relative_e_distances.value = relative;
                config.filament_diameter.values = {1.75, 2.85};
                config.filament_max_volumetric_flow.values = {0., 0.};
                if (limit_source == 1)
                    config.filament_max_volumetric_flow.values[tool] = 8.;
                config.max_volumetric_flow.value = limit_source == 2 ? 8. : 0.;
                config.max_volumetric_extrusion_rate_slope_positive.value = .00001;
                config.max_volumetric_extrusion_rate_slope_negative.value = .00001;
                PressureEqualizer equalizer(config);
                if (tool_in_header)
                    equalizer.set_current_extruder(tool);
                const std::string prefix = "G90\n" + std::string(relative ? "M83\n" : "M82\n") +
                    (tool_in_header ? "" : "T" + std::to_string(tool) + "\n") +
                    "G92 E0\n;_EXTRUSION_ROLE:" +
                    std::to_string(int(GCodeExtrusionRole::InterlockingPerimeter)) +
                    "\nG1 F30 ;_EXTRUDE_SET_SPEED\n";
                // At F30 the high moves are below 8 mm3/s for either diameter;
                // the legacy F60 floor actually exceeds 8, not just the input F.
                const std::string high = tool ? "2.50000" : "5.00000";
                const std::string first = "G1 X1 Y0 E" + high + " ; high\n";
                const std::string second = "G1 X2 Y0 E" + std::string(relative ? "0.00100" :
                    tool ? "2.50100" : "5.00100") + " ; low\n";
                const std::string third = "G1 X3 Y0 E" + (relative ? high :
                    tool ? std::string("5.00100") : std::string("10.00100")) + " ; high2\n";
                const std::string tail = "G1 X4 Y0 E" + std::string(relative ? "0.00100" :
                    tool ? "5.00200" : "10.00200") + "\nG1 X5 Y0 E" +
                    (relative ? high : tool ? std::string("7.50200") : std::string("15.00200")) + "\n";
                equalizer.process_layer({prefix + first + second + third + tail + ";_EXTRUDE_END\n", 0});
                bool modified = false;
                for (const auto &line : equalizer.m_gcode_lines)
                    modified = modified || line.modified;
                check(modified, "Fixture must activate slope adjustment");
                auto output = equalizer.process_layer(LayerResult::make_nop_layer_result()).gcode;
                if (limit_source) {
                    check(output.find(" F60") == std::string::npos, "Equalizer raised capped F30 to F60");
                    check(output.find(first) != std::string::npos, "First emitted E/XYZ changed");
                    check(output.find(second) != std::string::npos, "Second emitted E/XYZ changed");
                    check(output.find(third) != std::string::npos, "Third emitted E/XYZ changed");
                } else {
                    check(output.find(" F60") != std::string::npos, "Disabled-limit legacy control changed");
                }
            }
    std::cout << checks << " PressureEqualizer checks passed\n";
}
