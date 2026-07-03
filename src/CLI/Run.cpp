///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "../preFlight.hpp"
#include "CLI.hpp"
#include "libslic3r/DebugOutput.hpp"

#include <cctype>
#include <cstdio>
#include <string>

namespace Slic3r::CLI
{

// Parse --debug <comma-list|all> into the global debug mask. Unknown category
// names are a hard error so a forgotten category (which would otherwise swallow
// the next argument, e.g. --debug model.3mf) fails loudly instead of silently.
static bool apply_debug_flags(const Data &cli)
{
    if (!cli.misc_config.has("debug"))
        return true;

    const std::string spec = cli.misc_config.opt_string("debug");
    uint32_t mask = 0;
    size_t pos = 0;
    while (pos <= spec.size())
    {
        size_t comma = spec.find(',', pos);
        if (comma == std::string::npos)
            comma = spec.size();
        size_t b = pos, e = comma;
        while (b < e && std::isspace((unsigned char) spec[b]))
            ++b;
        while (e > b && std::isspace((unsigned char) spec[e - 1]))
            --e;
        if (e > b)
        {
            std::string token = spec.substr(b, e - b);
            uint32_t bit = debug_category_from_name(token);
            if (bit == 0)
            {
                std::fprintf(stderr,
                             "Unknown --debug category '%s'. Valid: fill, perimeters, interlock, serpentine, all.\n",
                             token.c_str());
                return false;
            }
            mask |= bit;
        }
        pos = comma + 1;
    }

    if (mask == 0)
    {
        std::fprintf(stderr, "--debug requires at least one category: fill, perimeters, interlock, serpentine, all.\n");
        return false;
    }

    g_debug_mask = mask;
    g_dbg_flusher.start(); // flush stdout on a cadence so redirected output lands live
    return true;
}

int run(int argc, char **argv)
{
    Data cli;
    if (!setup(cli, argc, argv))
        return 1;

    if (!apply_debug_flags(cli))
        return 1;

    if (process_profiles_sharing(cli))
        return 1;

    bool start_gui = cli.empty() || (cli.actions_config.empty() && !cli.transform_config.has("cut"));
    PrinterTechnology printer_technology = get_printer_technology(cli.overrides_config);
    DynamicPrintConfig print_config = {};
    std::vector<Model> models;

#ifdef SLIC3R_GUI
    GUI::GUI_InitParams gui_params;
    start_gui |= init_gui_params(gui_params, argc, argv, cli);

    if (gui_params.start_as_gcodeviewer)
        return start_as_gcode_viewer(gui_params);
#endif

    if (!load_print_data(models, print_config, printer_technology, cli))
        return 1;

    if (!start_gui && is_needed_post_processing(print_config))
        return 0;

    if (!process_transform(cli, print_config, models))
        return 1;

    if (!process_actions(cli, print_config, models))
        return 1;

    if (start_gui)
    {
#ifdef SLIC3R_GUI
        return start_gui_with_params(gui_params);
#else
        // No GUI support. Just print out a help.
        print_help(false);
        // If started without a parameter, consider it to be OK, otherwise report an error code (no action etc).
        return (argc == 0) ? 0 : 1;
#endif
    }

    return 0;
}

} // namespace Slic3r::CLI