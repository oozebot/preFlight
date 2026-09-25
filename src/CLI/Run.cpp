///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "../preFlight.hpp"
#include "CLI.hpp"
#include "luminary/core/diagnostics/DebugOutput.hpp"
#include "luminary/core/Prelude.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

#include <boost/nowide/iostream.hpp>

namespace Luminary::CLI
{

// Parse --debug <comma-list|all> into the global debug mask. Unknown category
// names are a hard error so a forgotten category (which would otherwise swallow
// the next argument, e.g. --debug model.3mf) fails loudly instead of silently.
static bool apply_debug_flags(const Data &cli)
{
    if (!cli.misc_config.has("debug"))
    {
        if (cli.misc_config.has("debug-z") || cli.misc_config.has("debug-geom"))
        {
            std::fprintf(stderr, "--debug-z and --debug-geom need --debug <categories>.\n");
            return false;
        }
        return true;
    }

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
                             "Unknown --debug category '%s'. Valid: fill, perimeters, interlock, serpentine, baobab, "
                             "support, stability, all.\n",
                             token.c_str());
                return false;
            }
            mask |= bit;
        }
        pos = comma + 1;
    }

    if (mask == 0)
    {
        std::fprintf(stderr, "--debug requires at least one category: fill, perimeters, interlock, serpentine, baobab, "
                             "support, stability, all.\n");
        return false;
    }

    g_debug_mask = mask;
    g_dbg_flusher.start(); // flush stdout on a cadence so redirected output lands live

    // --debug-z a[-b]: keep only the lines stamped inside that z window (z == 0 lines are
    // run-scoped and always pass). A single value is one exact z; callers widen as needed.
    if (cli.misc_config.has("debug-z"))
    {
        const std::string zspec = cli.misc_config.opt_string("debug-z");
        char *end = nullptr;
        double a = std::strtod(zspec.c_str(), &end);
        double b = a;
        if (end == zspec.c_str())
        {
            std::fprintf(stderr, "--debug-z expects <z> or <z_min>-<z_max>, got '%s'.\n", zspec.c_str());
            return false;
        }
        if (*end == '-')
        {
            char *end2 = nullptr;
            b = std::strtod(end + 1, &end2);
            if (end2 == end + 1)
            {
                std::fprintf(stderr, "--debug-z expects <z> or <z_min>-<z_max>, got '%s'.\n", zspec.c_str());
                return false;
            }
            end = end2;
        }
        if (*end != '\0')
        {
            // Trailing text would otherwise pass silently as the single value parsed so far.
            std::fprintf(stderr, "--debug-z expects <z> or <z_min>-<z_max>, got '%s'.\n", zspec.c_str());
            return false;
        }
        if (b < a)
            std::swap(a, b);
        g_debug_z_min = a;
        g_debug_z_max = b;
    }

    // Schema line first, so a parser can refuse an unknown major version before reading
    // counters or geometry.
    // counters_end=1 announces that every [COUNTER] block ends with an _END row-count line.
    dbg_log(DBG_ALL, 0., "META", "schema=2 build=%s categories=%s counters_end=1", PREFLIGHT_VERSION,
            debug_category_list(mask).c_str());
    if (cli.misc_config.has("debug-z"))
        dbg_log(DBG_ALL, 0., "META", "z_window=%.3f-%.3f", g_debug_z_min, g_debug_z_max);
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

    // The geometry sidecar is written next to a CLI gcode export; the GUI has no export path
    // to attach it to, so the flag is reported and ignored there.
    if (cli.misc_config.has("debug-geom") && cli.misc_config.opt_bool("debug-geom"))
    {
        if (start_gui)
            std::fprintf(stderr, "--debug-geom: the geometry sidecar requires a CLI export (-g); ignored.\n");
        else
            g_debug_geom = true;
    }
    PrinterTechnology printer_technology = get_printer_technology(cli.overrides_config);
    DynamicPrintConfig print_config = {};
    std::vector<Model> models;

#ifdef PREFLIGHT_GUI
    DSKY::GUI_InitParams gui_params;
    start_gui |= init_gui_params(gui_params, argc, argv, cli);

    if (gui_params.start_as_gcodeviewer)
        return start_as_gcode_viewer(gui_params);
#endif

    if (!load_print_data(models, print_config, printer_technology, cli))
        return 1;

    // A console run whose every input file was empty has no model to act on. The actions still run
    // (one that needs no model, such as saving the config, is unaffected); only the exit code says
    // so. The GUI opens on such a project as before, so this never applies when a window starts.
    const bool no_model_input = !start_gui && models.empty() && cli.empty_input_files > 0;

    if (!start_gui && is_needed_post_processing(print_config))
        return 0;

    if (!process_transform(cli, print_config, models))
        return 1;

    if (!process_actions(cli, print_config, models))
        return 1;

    if (start_gui)
    {
#ifdef PREFLIGHT_GUI
        return start_gui_with_params(gui_params);
#else
        // No GUI support. Just print out a help.
        print_help(false);
        // If started without a parameter, consider it to be OK, otherwise report an error code (no action etc).
        return (argc == 0) ? 0 : 1;
#endif
    }

    if (no_model_input)
    {
        boost::nowide::cerr << "Error: no input file holds a model." << std::endl;
        return 1;
    }

    return 0;
}

} // namespace Luminary::CLI