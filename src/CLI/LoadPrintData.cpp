///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include <cstdio>
#include <string>
#include <cstring>
#include <iostream>

#include <boost/filesystem.hpp>
#include <boost/nowide/args.hpp>
#include <boost/nowide/iostream.hpp>

#include "luminary/core/Prelude.hpp"
#include "luminary/config/model/Config.hpp"
#include "luminary/core/diagnostics/DebugCounters.hpp"
#include "luminary/gcode/postprocess/PostProcessor.hpp"
#include "luminary/model/scene/Model.hpp"
#include "luminary/format/reader/FileReader.hpp"

#include "CLI/CLI.hpp"
#include "CLI/ProfilesSharingUtils.hpp"

namespace Luminary::CLI
{

PrinterTechnology get_printer_technology(const DynamicConfig &config)
{
    const ConfigOptionEnum<PrinterTechnology> *opt = config.option<ConfigOptionEnum<PrinterTechnology>>(
        "printer_technology");
    return (opt == nullptr) ? ptUnknown : opt->value;
}

// preFlight prints FFF only, so a source naming another technology is not a load error. The value
// travels to finalize_print_config, which counts it and reads it as FFF; the last source that names
// a technology wins, so a value the build does not print always reaches the counter.
static void adopt_printer_technology(PrinterTechnology &printer_technology, const DynamicConfig &config)
{
    const PrinterTechnology other_printer_technology = get_printer_technology(config);
    if (other_printer_technology != ptUnknown)
        printer_technology = other_printer_technology;
}

static void print_config_substitutions(const ConfigSubstitutions &config_substitutions, const std::string &file)
{
    if (config_substitutions.empty())
        return;
    boost::nowide::cout << "The following configuration values were substituted when loading \" << file << \":\n";
    for (const ConfigSubstitution &subst : config_substitutions)
        boost::nowide::cout << "\tkey = \"" << subst.opt_def->opt_key << "\"\t loaded = \"" << subst.old_value
                            << "\tsubstituted = \"" << subst.new_value->serialize() << "\"\n";
}

static bool load_print_config(DynamicPrintConfig &print_config, PrinterTechnology &printer_technology, const Data &cli)
{
    // first of all load configuration from "--load" if any

    if (cli.input_config.has("load"))
    {
        const std::vector<std::string> &load_configs = cli.input_config.option<ConfigOptionStrings>("load")->values;
        ForwardCompatibilitySubstitutionRule config_substitution_rule =
            cli.misc_config.option<ConfigOptionEnum<ForwardCompatibilitySubstitutionRule>>("config_compatibility")
                ->value;

        // load config files supplied via --load
        for (auto const &file : load_configs)
        {
            if (!boost::filesystem::exists(file))
            {
                if (cli.misc_config.has("ignore_nonexistent_config") &&
                    cli.misc_config.opt_bool("ignore_nonexistent_config"))
                {
                    continue;
                }
                else
                {
                    boost::nowide::cerr << "No such file: " << file << std::endl;
                    return false;
                }
            }
            DynamicPrintConfig config;
            ConfigSubstitutions config_substitutions;
            try
            {
                config_substitutions = config.load(file, config_substitution_rule);
            }
            catch (std::exception &ex)
            {
                boost::nowide::cerr << "Error while reading config file \"" << file << "\": " << ex.what() << std::endl;
                return false;
            }

            adopt_printer_technology(printer_technology, config);

            print_config_substitutions(config_substitutions, file);

            config.normalize_fdm();
            print_config.apply(config);
        }
    }

    // than apply other options from full print config if any is provided by prifiles set

    if (has_full_config_from_profiles(cli))
    {
        DynamicPrintConfig config;
        // load config from profiles set
        std::string errors = Luminary::load_full_print_config(
            cli.input_config.opt_string("print-profile"),
            cli.input_config.option<ConfigOptionStrings>("material-profile")->values,
            cli.input_config.opt_string("printer-profile"), config, printer_technology);
        if (!errors.empty())
        {
            boost::nowide::cerr << "Error while loading config from profiles: " << errors << std::endl;
            return false;
        }

        adopt_printer_technology(printer_technology, config);

        config.normalize_fdm();

        // config is applied with print_config loaded before
        config += std::move(print_config);
        print_config = std::move(config);
    }

    return true;
}

static bool process_input_files(std::vector<Model> &models, DynamicPrintConfig &print_config,
                                PrinterTechnology &printer_technology, Data &cli)
{
    for (const std::string &file : cli.input_files)
    {
        if (boost::starts_with(file, "preflight://"))
        {
            continue;
        }
        if (!boost::filesystem::exists(file))
        {
            boost::nowide::cerr << "No such file: " << file << std::endl;
            return false;
        }

        Model model;
        try
        {
            if (has_full_config_from_profiles(cli) || !FileReader::is_project_file(file))
            {
                // we have full banch of options from profiles set
                // so, just load a geometry
                model = FileReader::load_model(file);
            }
            else
            {
                // load model and configuration from the file
                DynamicPrintConfig config;
                ConfigSubstitutionContext config_substitutions_ctxt(
                    cli.misc_config
                        .option<ConfigOptionEnum<ForwardCompatibilitySubstitutionRule>>("config_compatibility")
                        ->value);
                boost::optional<Semver> generator_version;

                // No version gate on the console path: a project written by a newer build still loads.
                model = FileReader::load_model_with_config(file, &config, &config_substitutions_ctxt, generator_version,
                                                           FileReader::LoadAttribute::AddDefaultInstances);

                adopt_printer_technology(printer_technology, config);

                print_config_substitutions(config_substitutions_ctxt.substitutions, file);

                // config is applied with print_config loaded before
                config += std::move(print_config);
                print_config = std::move(config);
            }

            // If model for slicing is loaded from 3mf file, then its geometry has to be used and arrange couldn't be apply for this model.
            if (FileReader::is_project_file(file) &&
                (!cli.transform_config.has("dont_arrange") || !cli.transform_config.opt_bool("dont_arrange")))
            {
                //So, check a state of "dont_arrange" parameter and set it to true, if its value is false.
                cli.transform_config.set_key_value("dont_arrange", new ConfigOptionBool(true));
            }
        }
        catch (std::exception &e)
        {
            boost::nowide::cerr << file << ": " << e.what() << std::endl;
            return false;
        }
        if (model.objects.empty())
        {
            boost::nowide::cerr << "Error: file is empty: " << file << std::endl;
            ++cli.empty_input_files;
            continue;
        }
        models.push_back(model);
    }

    return true;
}

static bool finalize_print_config(DynamicPrintConfig &print_config, PrinterTechnology &printer_technology,
                                  const Data &cli)
{
    // Apply command line options to a more specific DynamicPrintConfig which provides normalize()
    // (command line options override --load files or congiguration which is loaded prom profiles)
    print_config.apply(cli.overrides_config, true);
    // Normalizing after importing the 3MFs / AMFs
    print_config.normalize_fdm();

    // preFlight prints FFF only. A project or profile asking for another technology is counted and
    // read as FFF, so the file still loads and slices. The --export-sla action is refused outright
    // elsewhere, because that is a request for a capability rather than an old file.
    if (printer_technology != ptFFF)
    {
        if (printer_technology != ptUnknown)
            DBG_COUNT_LOAD("PRINTER_TECHNOLOGY_SLA_REJECTED");
        printer_technology = ptFFF;
    }
    print_config.option<ConfigOptionEnum<PrinterTechnology>>("printer_technology", true)->value = printer_technology;

    FullPrintConfig fff_print_config;
    fff_print_config.apply(print_config, true);
    print_config.apply(fff_print_config, true);

    // validate print configuration
    std::string validity = print_config.validate();
    if (!validity.empty())
    {
        boost::nowide::cerr << "Error: The composite configation is not valid: " << validity << std::endl;
        return false;
    }

    return true;
}

bool load_print_data(std::vector<Model> &models, DynamicPrintConfig &print_config,
                     PrinterTechnology &printer_technology, Data &cli)
{
    if (!load_print_config(print_config, printer_technology, cli))
        return false;

    if (!process_input_files(models, print_config, printer_technology, cli))
        return false;

    if (!finalize_print_config(print_config, printer_technology, cli))
        return false;

    return true;
}

bool is_needed_post_processing(const DynamicPrintConfig &print_config)
{
    if (print_config.has("post_process"))
    {
        const std::vector<std::string> &post_process = print_config.opt<ConfigOptionStrings>("post_process")->values;
        if (!post_process.empty())
        {
            boost::nowide::cout << "\nA post-processing script has been detected in the config data:\n\n";
            for (const std::string &s : post_process)
            {
                boost::nowide::cout << "> " << s << "\n";
            }
            boost::nowide::cout << "\nContinue(Y/N) ? ";
            char in;
            boost::nowide::cin >> in;
            if (in != 'Y' && in != 'y')
                return true;
        }
    }

    return false;
}

} // namespace Luminary::CLI