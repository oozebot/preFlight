///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <string>
#include <cstring>
#include <iostream>
#include <math.h>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/args.hpp>
#include <boost/nowide/cstdlib.hpp>
#include <boost/nowide/iostream.hpp>
#include <boost/nowide/filesystem.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/nowide/iostream.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/dll/runtime_symbol_info.hpp>

#include "luminary/core/Prelude.hpp"
#include "luminary/core/diagnostics/DebugCounters.hpp"
// resources_dir and rename_file, which this file reached transitively until Slicing.hpp stopped
// carrying the Utils.hpp umbrella.
#include "luminary/platform/paths/Paths.hpp"
#include "luminary/platform/files/FileIO.hpp"
#include "luminary/core/diagnostics/DebugOutput.hpp"
#if !PREFLIGHT_OPENGL_ES
#include <boost/algorithm/string/split.hpp>
#endif // !PREFLIGHT_OPENGL_ES
#include "luminary/config/model/Config.hpp"
#include "luminary/layer/settings_spec/SettingsSpec.hpp"
#include "luminary/geometry/transform/Geometry.hpp"
#include "luminary/gcode/postprocess/PostProcessor.hpp"
#include "luminary/gcode/interpret/GCodeProcessor.hpp"
#include "luminary/model/scene/Model.hpp"
#include "luminary/presets/preset/Preset.hpp"
#include <luminary/arrange/scene/ModelArrange.hpp>
#include "luminary/layer/print/Print.hpp"
#include "luminary/format/amf/AMF.hpp"
#include "luminary/format/threemf/3mf.hpp"
#include "luminary/format/mesh/STL.hpp"
#include "luminary/format/mesh/OBJ.hpp"
#include "luminary/io/zip/miniz_extension.hpp"
#include "luminary/io/png/PNGReadWrite.hpp"
#include "luminary/model/beds/MultipleBeds.hpp"
#include "luminary/model/build_volume/BuildVolume.hpp"

#include "CLI/CLI.hpp"
#include "CLI/ProfilesSharingUtils.hpp"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

namespace Luminary::CLI
{

static bool has_profile_sharing_action(const Data &cli)
{
    return cli.actions_config.has("query-printer-models") || cli.actions_config.has("query-print-filament-profiles");
}

bool has_full_config_from_profiles(const Data &cli)
{
    const DynamicPrintConfig &input = cli.input_config;
    return !has_profile_sharing_action(cli) &&
           (input.has("print-profile") && !input.opt_string("print-profile").empty() ||
            input.has("material-profile") && !input.option<ConfigOptionStrings>("material-profile")->values.empty() ||
            input.has("printer-profile") && !input.opt_string("printer-profile").empty());
}

bool process_profiles_sharing(const Data &cli)
{
    if (!has_profile_sharing_action(cli))
        return false;

    std::string ret;

    if (cli.actions_config.has("query-printer-models"))
    {
        ret = Luminary::get_json_printer_models();
    }
    else if (cli.actions_config.has("query-print-filament-profiles"))
    {
        if (cli.input_config.has("printer-profile") && !cli.input_config.opt_string("printer-profile").empty())
        {
            const std::string printer_profile = cli.input_config.opt_string("printer-profile");
            ret = Luminary::get_json_print_filament_profiles(printer_profile);
            if (ret.empty())
            {
                boost::nowide::cerr << "query-print-filament-profiles error: Printer profile '" << printer_profile
                                    << "' wasn't found among installed printers." << std::endl
                                    << "Or the request can be wrong." << std::endl;
                return true;
            }
        }
        else
        {
            boost::nowide::cerr
                << "query-print-filament-profiles error: This action requires set 'printer-profile' option"
                << std::endl;
            return true;
        }
    }

    if (ret.empty())
    {
        boost::nowide::cerr << "Wrong request" << std::endl;
        return true;
    }

    // use --output when available

    if (cli.misc_config.has("output"))
    {
        std::string cmdline_param = cli.misc_config.opt_string("output");
        // if we were supplied a directory, use it and append our automatically generated filename
        boost::filesystem::path cmdline_path(cmdline_param);
        boost::filesystem::path proposed_path = boost::filesystem::path(Luminary::resources_dir()) / "out.json";
        if (boost::filesystem::is_directory(cmdline_path))
            proposed_path = (cmdline_path / proposed_path.filename());
        else if (cmdline_path.extension().empty())
            proposed_path = cmdline_path.replace_extension("json");
        else
            proposed_path = cmdline_path;
        const std::string file = proposed_path.string();

        boost::nowide::ofstream c;
        c.open(file, std::ios::out | std::ios::trunc);
        c << ret << std::endl;
        c.close();

        boost::nowide::cout << "Output for your request is written into " << file << std::endl;
    }
    else
        printf("%s", ret.c_str());

    return true;
}

namespace IO
{
enum ExportFormat : int
{
    OBJ,
    STL,
    // SVG,
    TMF,
    Gcode
};
}

static std::string output_filepath(const Model &model, IO::ExportFormat format, const std::string &cmdline_param)
{
    std::string ext;
    switch (format)
    {
    case IO::OBJ:
        ext = ".obj";
        break;
    case IO::STL:
        ext = ".stl";
        break;
    case IO::TMF:
        ext = ".3mf";
        break;
    default:
        assert(false);
        break;
    };
    auto proposed_path = boost::filesystem::path(model.propose_export_file_name_and_path(ext));
    // use --output when available
    if (!cmdline_param.empty())
    {
        // if we were supplied a directory, use it and append our automatically generated filename
        boost::filesystem::path cmdline_path(cmdline_param);
        if (boost::filesystem::is_directory(cmdline_path))
            proposed_path = cmdline_path / proposed_path.filename();
        else
            proposed_path = cmdline_path;
    }
    return proposed_path.string();
}

static bool export_models(std::vector<Model> &models, IO::ExportFormat format, const std::string &cmdline_param)
{
    for (Model &model : models)
    {
        const std::string path = output_filepath(model, format, cmdline_param);
        bool success = false;
        switch (format)
        {
        case IO::OBJ:
            success = Luminary::store_obj(path.c_str(), &model);
            break;
        case IO::STL:
            success = Luminary::store_stl(path.c_str(), &model, true);
            break;
        case IO::TMF:
            success = Luminary::store_3mf(path.c_str(), &model, nullptr, false);
            break;
        default:
            assert(false);
            break;
        }
        if (success)
            std::cout << "File exported to " << path << std::endl;
        else
        {
            std::cerr << "File export to " << path << " failed" << std::endl;
            return false;
        }
    }
    return true;
}

static ThumbnailData resize_and_crop(const std::vector<unsigned char> &data, int width, int height, int width_new,
                                     int height_new)
{
    ThumbnailData th;

    float scale_x = float(width_new) / width;
    float scale_y = float(height_new) / height;
    float scale = std::max(scale_x, scale_y); // Choose the larger scale to fill the box
    int resized_width = int(width * scale);
    int resized_height = int(height * scale);

    std::vector<unsigned char> resized_rgba(resized_width * resized_height * 4);
    stbir_resize_uint8_linear(data.data(), width, height, 4 * width, resized_rgba.data(), resized_width, resized_height,
                              4 * resized_width, STBIR_RGBA);

    th.set(width_new, height_new);
    int crop_x = (resized_width - width_new) / 2;
    int crop_y = (resized_height - height_new) / 2;

    for (int y = 0; y < height_new; ++y)
    {
        std::memcpy(th.pixels.data() + y * width_new * 4,
                    resized_rgba.data() + ((y + crop_y) * resized_width + crop_x) * 4, width_new * 4);
    }
    return th;
}

static std::function<ThumbnailsList(const ThumbnailsParams &)> get_thumbnail_generator_cli(const std::string &filename)
{
    if (boost::iends_with(filename, ".3mf"))
    {
        return [filename](const ThumbnailsParams &params)
        {
            ThumbnailsList list_out;

            mz_zip_archive archive;
            mz_zip_zero_struct(&archive);

            if (!open_zip_reader(&archive, filename))
                return list_out;
            mz_uint num_entries = mz_zip_reader_get_num_files(&archive);
            mz_zip_archive_file_stat stat;

            int index = mz_zip_reader_locate_file(&archive, "Metadata/thumbnail.png", nullptr, 0);
            if (index < 0 || !mz_zip_reader_file_stat(&archive, index, &stat))
                return list_out;
            std::string buffer;
            buffer.resize(int(stat.m_uncomp_size));
            mz_bool res = mz_zip_reader_extract_file_to_mem(&archive, stat.m_filename, buffer.data(),
                                                            (size_t) stat.m_uncomp_size, 0);
            if (res == 0)
                return list_out;
            close_zip_reader(&archive);

            std::vector<unsigned char> data;
            unsigned width = 0;
            unsigned height = 0;
            png::decode_png(buffer, data, width, height);

            {
                // Flip the image vertically so it matches the convention in Thumbnails generator.
                const int row_size = width * 4; // Each pixel is 4 bytes (RGBA)
                std::vector<unsigned char> temp_row(row_size);
                for (int i = 0; i < height / 2; ++i)
                {
                    unsigned char *top_row = &data[i * row_size];
                    unsigned char *bottom_row = &data[(height - i - 1) * row_size];
                    std::copy(bottom_row, bottom_row + row_size, temp_row.begin());
                    std::copy(top_row, top_row + row_size, bottom_row);
                    std::copy(temp_row.begin(), temp_row.end(), top_row);
                }
            }

            for (const Vec2d &size : params.sizes)
            {
                Point isize(size);
                list_out.push_back(resize_and_crop(data, width, height, isize.x(), isize.y()));
            }
            return list_out;
        };
    }

    return [](const ThumbnailsParams &) -> ThumbnailsList
    {
        return {};
    };
}

static void update_instances_outside_state(Model &model, const DynamicPrintConfig &config)
{
    Pointfs bed_shape = dynamic_cast<const ConfigOptionPoints *>(config.option("bed_shape"))->values;
    BuildVolume build_volume(bed_shape, config.opt_float("max_print_height"));
    s_multiple_beds.update_build_volume(BoundingBoxf(bed_shape));
    model.update_print_volume_state(build_volume);
}

// --dump-config-defs: every print option definition as one JSON object per key, so a harness
// can generate value tables (toggle every key) without a hand-written list.
static void dump_config_defs()
{
    const auto json_string = [](const std::string &s)
    {
        std::string out = "\"";
        for (const char c : s)
        {
            switch (c)
            {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                    out += ' ';
                else
                    out += c;
            }
        }
        return out + "\"";
    };
    const auto type_name = [](ConfigOptionType type) -> const char *
    {
        switch (type)
        {
        case coFloat:
            return "float";
        case coFloats:
            return "floats";
        case coInt:
            return "int";
        case coInts:
            return "ints";
        case coString:
            return "string";
        case coStrings:
            return "strings";
        case coPercent:
            return "percent";
        case coPercents:
            return "percents";
        case coFloatOrPercent:
            return "float_or_percent";
        case coFloatsOrPercents:
            return "floats_or_percents";
        case coPoint:
            return "point";
        case coPoints:
            return "points";
        case coPoint3:
            return "point3";
        case coBool:
            return "bool";
        case coBools:
            return "bools";
        case coEnum:
            return "enum";
        default:
            return "other";
        }
    };
    const std::vector<std::string> object_keys = PrintObjectConfig().keys();
    const std::vector<std::string> region_keys = PrintRegionConfig().keys();
    const auto in = [](const std::vector<std::string> &keys, const std::string &key)
    {
        return std::find(keys.begin(), keys.end(), key) != keys.end();
    };
    boost::nowide::cout << "{\n";
    bool first = true;
    for (const auto &[key, def] : print_config_def.options)
    {
        const char *scope = in(object_keys, key) ? "object" : in(region_keys, key) ? "region" : "print";
        boost::nowide::cout << (first ? "" : ",\n") << json_string(key) << ": {\"type\": \"" << type_name(def.type)
                            << "\", \"scope\": \"" << scope << "\", \"default\": "
                            << json_string(def.default_value ? def.default_value->serialize() : std::string())
                            << ", \"min\": " << def.min << ", \"max\": "
                            << def.max
                            // The presentation fields the G-code trailer never carries, so a hash of this
                            // dump gates them: labels, tooltips, category, mode, side text, aliases.
                            << ", \"mode\": " << int(def.mode)
                            << ", \"readonly\": " << (def.readonly ? "true" : "false")
                            << ", \"category\": " << json_string(def.category)
                            << ", \"label\": " << json_string(def.label)
                            << ", \"full_label\": " << json_string(def.full_label)
                            << ", \"tooltip\": " << json_string(def.tooltip)
                            << ", \"sidetext\": " << json_string(def.sidetext) << ", \"aliases\": [";
        for (size_t i = 0; i < def.aliases.size(); ++i)
            boost::nowide::cout << (i ? ", " : "") << json_string(def.aliases[i]);
        boost::nowide::cout << "], \"enum\": [";
        if (def.enum_def && def.enum_def->has_values())
        {
            bool first_value = true;
            for (const std::string &value : def.enum_def->values())
            {
                boost::nowide::cout << (first_value ? "" : ", ") << json_string(value);
                first_value = false;
            }
        }
        boost::nowide::cout << "]}";
        first = false;
    }
    boost::nowide::cout << "\n}\n";
}

// Step names in enum order for the --reslice report.
static const char *const s_print_step_names[psCount] = {"psWipeTower", "psAlertWhenSupportsNeeded", "psSkirtBrim",
                                                        "psGCodeExport"};
static const char *const s_object_step_names[posCount] = {"posSlice",
                                                          "posPerimeters",
                                                          "posPrepareInfill",
                                                          "posBridgeOverInfill",
                                                          "posInfill",
                                                          "posIroning",
                                                          "posSupportSpotsSearch",
                                                          "posSupportMaterial",
                                                          "posEstimateCurledExtrusions",
                                                          "posCalculateOverhangingPerimeters"};

// --reslice diagnostics: a fingerprint of everything Print::apply diffs on the model (instance
// and volume matrices, mesh data, paint and config timestamps), taken before the first apply
// and again after the first export, so a process that mutates the model is named.
static void model_fingerprint(const Model &model, std::map<std::string, uint64_t> &out)
{
    const auto fnv = [](const void *data, size_t size)
    {
        uint64_t h = 1469598103934665603ull;
        const unsigned char *p = static_cast<const unsigned char *>(data);
        for (size_t i = 0; i < size; ++i)
        {
            h ^= p[i];
            h *= 1099511628211ull;
        }
        return h;
    };
    const auto matrix_hash = [&fnv](const Transform3d &m)
    {
        return fnv(m.data(), 16 * sizeof(double));
    };
    size_t object_idx = 0;
    for (const ModelObject *mo : model.objects)
    {
        const std::string o = "object[" + std::to_string(object_idx++) + "].";
        out[o + "origin_translation"] = fnv(mo->origin_translation.data(), 3 * sizeof(double));
        out[o + "layer_height_profile.timestamp"] = mo->layer_height_profile.timestamp();
        size_t instance_idx = 0;
        for (const ModelInstance *mi : mo->instances)
            out[o + "instance[" + std::to_string(instance_idx++) + "].matrix"] = matrix_hash(
                mi->get_transformation().get_matrix());
        size_t volume_idx = 0;
        for (const ModelVolume *mv : mo->volumes)
        {
            const std::string v = o + "volume[" + std::to_string(volume_idx++) + "].";
            out[v + "matrix"] = matrix_hash(mv->get_transformation().get_matrix());
            out[v + "type"] = uint64_t(mv->type());
            const indexed_triangle_set &its = mv->mesh().its;
            out[v + "mesh.vertices"] = fnv(its.vertices.data(), its.vertices.size() * sizeof(its.vertices[0]));
            out[v + "mesh.indices"] = fnv(its.indices.data(), its.indices.size() * sizeof(its.indices[0]));
            out[v + "supported_facets.timestamp"] = mv->supported_facets.timestamp();
            out[v + "seam_facets.timestamp"] = mv->seam_facets.timestamp();
            out[v + "mm_segmentation_facets.timestamp"] = mv->mm_segmentation_facets.timestamp();
            out[v + "fuzzy_skin_facets.timestamp"] = mv->fuzzy_skin_facets.timestamp();
            out[v + "counterbore_bridge_facets.timestamp"] = mv->counterbore_bridge_facets.timestamp();
            out[v + "color_mixing_facets.timestamp"] = mv->color_mixing_facets.timestamp();
        }
    }
}

static void report_model_changes(const std::map<std::string, uint64_t> &before,
                                 const std::map<std::string, uint64_t> &after)
{
    std::string changed;
    for (const auto &[key, value] : after)
    {
        auto it = before.find(key);
        if (it == before.end() || it->second != value)
            changed += ' ' + key;
    }
    for (const auto &[key, value] : before)
        if (after.find(key) == after.end())
            changed += " -" + key;
    boost::nowide::cout << "[RESLICE] model"
                        << (changed.empty() ? " unchanged by the first slice" : " changed:" + changed) << std::endl;
}

// --reslice: apply the key = value lines of delta_path on top of print_config to the sliced
// print, print the steps the change invalidated, process again and export next to outfile with
// a .re suffix. The apply, process and export wall times are printed; a fresh slice with the
// same delta must be byte-identical to the re-sliced file.
static bool reslice_with_delta(const std::string &delta_path, const DynamicPrintConfig &print_config, Model &model,
                               Print &fff_print, const std::string &outfile)
{
    using clock = std::chrono::steady_clock;
    const auto ms = [](clock::time_point a, clock::time_point b)
    {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };

    DynamicPrintConfig delta;
    try
    {
        delta.load(delta_path, ForwardCompatibilitySubstitutionRule::Enable);
    }
    catch (const std::exception &ex)
    {
        boost::nowide::cerr << "Error while reading reslice config \"" << delta_path << "\": " << ex.what()
                            << std::endl;
        return false;
    }
    DynamicPrintConfig config = print_config;
    config.apply(delta, true);
    config.normalize_fdm();
    if (const std::string validity = config.validate(); !validity.empty())
    {
        boost::nowide::cerr << "Error: The reslice configuration is not valid: " << validity << std::endl;
        return false;
    }

    const auto t0 = clock::now();
    MultipleBedsUtils::with_single_bed_model_fff(model, 0,
                                                 [&fff_print, &model, &config]() { fff_print.apply(model, config); });
    const auto t1 = clock::now();

    boost::nowide::cout << "[RESLICE] delta:";
    for (const std::string &key : delta.keys())
        boost::nowide::cout << ' ' << key << '=' << delta.opt_serialize(key);
    boost::nowide::cout << '\n';
    size_t object_idx = 0;
    for (const PrintObject *po : fff_print.objects())
    {
        boost::nowide::cout << "[RESLICE] object " << object_idx++ << " invalidated:";
        for (int step = 0; step < int(posCount); ++step)
            if (!po->is_step_done(PrintObjectStep(step)))
                boost::nowide::cout << ' ' << s_object_step_names[step];
        boost::nowide::cout << '\n';
    }
    boost::nowide::cout << "[RESLICE] print invalidated:";
    for (int step = 0; step < int(psCount); ++step)
        if (!fff_print.is_step_done(PrintStep(step)))
            boost::nowide::cout << ' ' << s_print_step_names[step];
    boost::nowide::cout << std::endl;

    if (const std::string err = fff_print.validate(); !err.empty())
    {
        boost::nowide::cerr << err << std::endl;
        return false;
    }
    fff_print.process();
    const auto t2 = clock::now();

    std::string re_path = outfile;
    if (boost::algorithm::iends_with(re_path, ".gcode"))
        re_path.insert(re_path.size() - 6, ".re");
    else
        re_path += ".re.gcode";
    GCodeProcessorResult result;
    const std::string input_file = fff_print.model().objects.empty() ? ""
                                                                     : fff_print.model().objects.front()->input_file;
    re_path = fff_print.export_gcode(re_path, &result, get_thumbnail_generator_cli(input_file));
    run_post_process_scripts(re_path, fff_print.full_print_config());
    if (result.is_binary_file && result.binary_data.has_value())
    {
        const std::string text_path = re_path + ".tmp";
        boost::filesystem::rename(re_path, text_path);
        GCodeProcessor::write_binary_gcode_from_file(re_path, text_path, *result.binary_data);
        boost::filesystem::remove(text_path);
    }
    const auto t3 = clock::now();

    boost::nowide::cout << "[RESLICE] apply_ms=" << ms(t0, t1) << " process_ms=" << ms(t1, t2)
                        << " export_ms=" << ms(t2, t3) << std::endl;
    boost::nowide::cout << "Reslice result exported to " << re_path << std::endl;
    return true;
}

bool process_actions(Data &cli, const DynamicPrintConfig &print_config, std::vector<Model> &models)
{
    DynamicPrintConfig &actions = cli.actions_config;
    DynamicPrintConfig &transform = cli.transform_config;

    // doesn't need any aditional input

    if (actions.has("help"))
    {
        print_help();
    }
    if (actions.has("help_fff"))
    {
        print_help(true, ptFFF);
    }

    if (actions.has("dump_config_defs"))
    {
        dump_config_defs();
    }

    if (actions.has("check_settings_spec"))
    {
        const int failures = check_settings_spec(boost::nowide::cerr);
        boost::nowide::cout << "settings spec: " << failures << " failure(s)" << std::endl;
        if (failures > 0)
            return false;
    }

    if (actions.has("info"))
    {
        if (models.empty())
        {
            boost::nowide::cerr << "error: cannot show info for empty models." << std::endl;
            return false;
        }
        // --info works on unrepaired model
        for (Model &model : models)
        {
            model.add_default_instances();
            model.print_info();
        }
    }

    if (actions.has("save"))
    {
        print_config.save(actions.opt_string("save"));
    }

    if (models.empty() && (actions.has("export_stl") || actions.has("export_obj") || actions.has("export_3mf")))
    {
        boost::nowide::cerr << "error: cannot export empty models." << std::endl;
        return false;
    }

    const std::string output = cli.misc_config.has("output") ? cli.misc_config.opt_string("output") : "";

    if (actions.has("export_stl"))
    {
        for (auto &model : models)
            model.add_default_instances();
        if (!export_models(models, IO::STL, output))
            return false;
    }
    if (actions.has("export_obj"))
    {
        for (auto &model : models)
            model.add_default_instances();
        if (!export_models(models, IO::OBJ, output))
            return false;
    }
    if (actions.has("export_3mf"))
    {
        if (!export_models(models, IO::TMF, output))
            return false;
    }

    if (actions.has("slice") || actions.has("export_gcode"))
    {
        const Vec2crd gap{s_multiple_beds.get_bed_gap()};
        arr2::ArrangeBed bed = arr2::to_arrange_bed(get_bed_shape(print_config), gap);
        arr2::ArrangeSettings arrange_cfg;
        arrange_cfg.set_distance_from_objects(min_object_distance(print_config));

        for (Model &model : models)
        {
            // If all objects have defined instances, their relative positions will be
            // honored when printing (they will be only centered, unless --dont-arrange
            // is supplied); if any object has no instances, it will get a default one
            // and all instances will be rearranged (unless --dont-arrange is supplied).
            if (!transform.has("dont_arrange") || !transform.opt_bool("dont_arrange"))
            {
                if (transform.has("center"))
                {
                    Vec2d c = transform.option<ConfigOptionPoint>("center")->value;
                    arrange_objects(model, arr2::InfiniteBed{scaled(c)}, arrange_cfg);
                }
                else
                    arrange_objects(model, bed, arrange_cfg);
            }

            Print fff_print;
            PrintBase *print = static_cast<PrintBase *>(&fff_print);
            for (auto *mo : model.objects)
                fff_print.auto_assign_extruders(mo);

#ifdef PREFLIGHT_PYTHON_PREPROCESSOR
            // Preprocessing scripts run in the console only on explicit request. --allow-scripts grants the
            // consent the GUI collects through its dialog, uses the default category order and resolves
            // relative script paths against the first input file's directory. Without it, scripts enabled in
            // the loaded profiles are skipped with a visible warning rather than silently.
            bool scripts_configured = false;
            for (const char *enabled_key :
                 {"preprocessing_enabled_print", "preprocessing_enabled_filament", "preprocessing_enabled_printer"})
            {
                const auto *opt = print_config.opt<ConfigOptionBool>(enabled_key);
                if (opt != nullptr && opt->value)
                    scripts_configured = true;
            }
            const bool allow_scripts = cli.misc_config.has("allow-scripts") &&
                                       cli.misc_config.opt_bool("allow-scripts");
            if (allow_scripts)
            {
                fff_print.set_preprocessing_consent(true);
                fff_print.set_preprocessing_category_order("print,filament,printer");
                if (cli.misc_config.has("script-timeout"))
                    fff_print.set_preprocessing_timeout_seconds(
                        std::max(0.0, cli.misc_config.opt_float("script-timeout")) * 60.0);
                if (!model.objects.empty() && !model.objects.front()->input_file.empty())
                    fff_print.set_project_dir(
                        boost::filesystem::path(model.objects.front()->input_file).parent_path().string());
                boost::nowide::cout << "[PP] preprocessing scripts enabled by --allow-scripts"
                                    << (scripts_configured ? "" : " (none enabled in the loaded profiles)")
                                    << std::endl;
            }
            else if (scripts_configured)
            {
                boost::nowide::cerr << "Warning: preprocessing scripts are enabled in the loaded profiles but "
                                       "skipped; pass --allow-scripts to run them."
                                    << std::endl;
            }
#endif

            update_instances_outside_state(model, print_config);
            const bool reslice_requested = cli.misc_config.has("reslice") &&
                                           !cli.misc_config.opt_string("reslice").empty();
            std::map<std::string, uint64_t> model_before;
            if (reslice_requested)
                model_fingerprint(model, model_before);
            MultipleBedsUtils::with_single_bed_model_fff(model, 0, [&print, &model, &print_config]()
                                                         { print->apply(model, print_config); });

            std::string err = print->validate();
            if (!err.empty())
            {
                boost::nowide::cerr << err << std::endl;
                return false;
            }

            std::string outfile = output;

            if (print->empty())
                boost::nowide::cout << "Nothing to print for " << outfile
                                    << " . Either the print is empty or no object is fully inside the print volume."
                                    << std::endl;
            else
                try
                {
                    std::string outfile_final;
                    // --debug-geom: the sidecar sits next to the output path as given (before
                    // placeholder expansion); its path is printed once so a harness can find it.
                    if (Luminary::g_debug_geom)
                    {
                        const std::string sidecar = outfile + ".geom.wkt";
                        if (Luminary::dbg_geom_open(sidecar))
                            boost::nowide::cout << "Geometry sidecar: " << sidecar << std::endl;
                        else
                            boost::nowide::cerr << "Cannot open geometry sidecar " << sidecar << std::endl;
                    }
                    const auto t_process = std::chrono::steady_clock::now();
                    print->process();
                    const auto t_export = std::chrono::steady_clock::now();
                    // Pipeline geometry is in object space; the instance shifts let a reader
                    // place the records in print coordinates: one I|object|instance|dx|dy|name
                    // record each. The name is archive text: record separators are replaced.
                    if (Luminary::g_geom_file != nullptr)
                    {
                        size_t object_idx = 0;
                        for (const PrintObject *po : fff_print.objects())
                        {
                            std::string name = po->model_object()->name;
                            for (char &c : name)
                                if (c == '|' || c == '\n' || c == '\r')
                                    c = '_';
                            size_t instance_idx = 0;
                            for (const PrintInstance &pi : po->instances())
                                std::fprintf(Luminary::g_geom_file, "I|%zu|%zu|%.4f|%.4f|%s\n", object_idx,
                                             instance_idx++, unscaled<double>(pi.shift.x()),
                                             unscaled<double>(pi.shift.y()), name.c_str());
                            ++object_idx;
                        }
                    }
                    GCodeProcessorResult gcode_result;
                    // The outfile is processed by a PlaceholderParser.
                    const std::string input_file = fff_print.model().objects.empty()
                                                       ? ""
                                                       : fff_print.model().objects.front()->input_file;
                    outfile = fff_print.export_gcode(outfile, &gcode_result, get_thumbnail_generator_cli(input_file));
                    outfile_final = fff_print.print_statistics().finalize_output_path(outfile);
                    if (outfile != outfile_final)
                    {
                        if (Luminary::rename_file(outfile, outfile_final))
                        {
                            boost::nowide::cerr << "Renaming file " << outfile << " to " << outfile_final << " failed"
                                                << std::endl;
                            Luminary::dbg_geom_close();
                            return false;
                        }
                        outfile = outfile_final;
                    }
                    // The sidecar was opened under the -o name before slicing; follow the gcode to
                    // its placeholder-expanded name so `<gcode>.geom.wkt` holds.
                    if (Luminary::g_geom_file != nullptr && output + ".geom.wkt" != outfile + ".geom.wkt")
                    {
                        Luminary::dbg_geom_close();
                        if (Luminary::rename_file(output + ".geom.wkt", outfile + ".geom.wkt"))
                            boost::nowide::cerr << "Renaming geometry sidecar to " << outfile + ".geom.wkt"
                                                << " failed; it stays at " << output + ".geom.wkt" << std::endl;
                        else
                            boost::nowide::cout << "Geometry sidecar: " << outfile + ".geom.wkt" << std::endl;
                    }
                    // Run the post-processing scripts on the text gcode file.
                    run_post_process_scripts(outfile, fff_print.full_print_config());

                    // Binarize after scripts have had their chance to modify the text
                    if (gcode_result.is_binary_file && gcode_result.binary_data.has_value())
                    {
                        std::string text_path = outfile + ".tmp";
                        boost::filesystem::rename(outfile, text_path);
                        GCodeProcessor::write_binary_gcode_from_file(outfile, text_path, *gcode_result.binary_data);
                        boost::filesystem::remove(text_path);
                    }

                    boost::nowide::cout << "Slicing result exported to " << outfile << std::endl;
                    if (reslice_requested)
                    {
                        std::map<std::string, uint64_t> model_after;
                        model_fingerprint(model, model_after);
                        report_model_changes(model_before, model_after);
                        // The first slice's phase times, so one run yields both the full-slice
                        // and the re-slice cost of the same delta.
                        const auto t_done = std::chrono::steady_clock::now();
                        boost::nowide::cout
                            << "[RESLICE] first process_ms="
                            << std::chrono::duration<double, std::milli>(t_export - t_process).count()
                            << " export_ms=" << std::chrono::duration<double, std::milli>(t_done - t_export).count()
                            << std::endl;
                        if (!reslice_with_delta(cli.misc_config.opt_string("reslice"), print_config, model, fff_print,
                                                outfile))
                        {
                            Luminary::dbg_geom_close();
                            return false;
                        }
                    }
                    Luminary::dbg_geom_close();
                    // Final totals including anything the gcode export stage counted after the
                    // block Print::process() printed; a parser keeps the last value per name.
                    if (Luminary::g_debug_mask != 0)
                        Luminary::DbgCounters::inst().dump("EXPORT");
                }
                catch (const std::exception &ex)
                {
                    Luminary::dbg_geom_close();
                    boost::nowide::cerr << ex.what() << std::endl;
                    return false;
                }
        }
    }

    return true;
}

} // namespace Luminary::CLI