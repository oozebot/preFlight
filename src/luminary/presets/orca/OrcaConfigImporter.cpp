///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "OrcaConfigImporter.hpp"
#include "luminary/macro/orca_aliases/OrcaGCodeAliases.hpp"
#include "OrcaKeyMapping.hpp"
#include "luminary/presets/bundle/PresetBundle.hpp"
#include "luminary/platform/paths/Paths.hpp"
#include "luminary/io/zip/miniz_extension.hpp"

#include <regex>
#include <set>

#include <boost/algorithm/string/join.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>

#include <miniz.h>
#include "nlohmann/json.hpp"

#include "luminary/core/Prelude.hpp"

namespace Luminary
{

// -----------------------------------------------------------------------
// Manifest parsing
// -----------------------------------------------------------------------

OrcaConfigImporter::BundleManifest OrcaConfigImporter::parse_manifest(const std::string &json_content)
{
    BundleManifest manifest;
    try
    {
        auto j = nlohmann::json::parse(json_content);
        manifest.bundle_id = j.value("bundle_id", "");
        manifest.bundle_type = j.value("bundle_type", "");
        manifest.printer_preset_name = j.value("printer_preset_name", "");
        manifest.version = j.value("version", "");

        if (j.contains("printer_config") && j["printer_config"].is_array())
            for (const auto &v : j["printer_config"])
                manifest.printer_configs.push_back(v.get<std::string>());

        if (j.contains("filament_config") && j["filament_config"].is_array())
            for (const auto &v : j["filament_config"])
                manifest.filament_configs.push_back(v.get<std::string>());

        if (j.contains("process_config") && j["process_config"].is_array())
            for (const auto &v : j["process_config"])
                manifest.process_configs.push_back(v.get<std::string>());
    }
    catch (const std::exception &e)
    {
        BOOST_LOG_TRIVIAL(error) << "OrcaImporter: Failed to parse manifest: " << e.what();
    }
    return manifest;
}

// -----------------------------------------------------------------------
// ZIP helpers
// -----------------------------------------------------------------------

std::string OrcaConfigImporter::read_zip_entry(void *zip_ptr, const std::string &entry_name)
{
    auto *zip = static_cast<mz_zip_archive *>(zip_ptr);

    int index = mz_zip_reader_locate_file(zip, entry_name.c_str(), nullptr, 0);
    if (index < 0)
        return {};

    size_t uncompressed_size = 0;
    void *data = mz_zip_reader_extract_to_heap(zip, (mz_uint) index, &uncompressed_size, 0);
    if (!data)
        return {};

    std::string result(static_cast<const char *>(data), uncompressed_size);
    mz_free(data);
    return result;
}

// -----------------------------------------------------------------------
// GCode placeholder translation
// -----------------------------------------------------------------------

std::string OrcaConfigImporter::translate_gcode(const std::string &orca_gcode, const std::string &profile_name,
                                                const std::string &field_name, std::vector<std::string> &warnings)
{
    std::string result = orca_gcode;

    // preFlight's PlaceholderParser natively supports both [bracket] (legacy) and
    // {brace} syntax, and resolves Orca key names via the alias map in
    // OrcaGCodeAliases. So we do NOT convert [brackets] to {braces} here.
    //
    // The one case that still needs fixup: {brace} syntax with a vector variable but no
    // index. The brace-syntax parser requires an explicit index for vectors (unlike the
    // bracket-syntax parser which defaults to current_extruder_id). Some Orca profiles
    // use {brace} syntax directly, so we append [0] where needed.

    const auto &aliases = orca_gcode_aliases();

    try
    {
        std::regex brace_pattern(R"(\{([a-z_][a-z_0-9]*)\})");
        std::string out;
        auto it = std::sregex_iterator(result.begin(), result.end(), brace_pattern);
        auto end = std::sregex_iterator();
        size_t last_pos = 0;
        for (; it != end; ++it)
        {
            const std::smatch &m = *it;
            out.append(result, last_pos, m.position() - last_pos);
            std::string key = m[1].str();

            // Look up the config definition, resolving Orca aliases if needed
            const auto *optdef = print_config_def.get(key);
            if (optdef == nullptr)
            {
                auto alias_it = aliases.find(key);
                if (alias_it != aliases.end())
                    optdef = print_config_def.get(alias_it->second);
            }

            if (optdef && !optdef->is_scalar())
                out += "{" + key + "[0]}";
            else
                out += m[0].str();
            last_pos = m.position() + m.length();
        }
        out.append(result, last_pos);
        result = std::move(out);
    }
    catch (...)
    {
    }

    // Warn about Orca-specific placeholders that have no preFlight equivalent.
    // Check both [bracket] and {brace} syntax since either may be present.
    static const std::set<std::string> orca_specific = {"flush_length",
                                                        "timelapse_pos_x",
                                                        "timelapse_pos_y",
                                                        "outer_wall_volumetric_speed",
                                                        "first_flush_volume",
                                                        "second_flush_volume",
                                                        "old_filament_e_feedrate",
                                                        "new_filament_e_feedrate",
                                                        "old_retract_length_toolchange",
                                                        "new_retract_length_toolchange"};

    for (const auto &ph : orca_specific)
    {
        std::string brace_search = "{" + ph + "}";
        std::string bracket_search = "[" + ph + "]";
        if (result.find(brace_search) != std::string::npos || result.find(bracket_search) != std::string::npos)
        {
            warnings.push_back(profile_name + ": Orca-only placeholder '" + ph + "' in " + field_name +
                               " has no preFlight equivalent");
        }
    }

    return result;
}

// -----------------------------------------------------------------------
// Bed temperature lossy mapping
// -----------------------------------------------------------------------

void OrcaConfigImporter::map_bed_temperatures(const nlohmann::json &j, DynamicPrintConfig &config,
                                              bool is_initial_layer, const std::string &profile_name,
                                              ImportResult &result)
{
    // Priority order: hot_plate > textured_plate > supertack > eng_plate > textured_cool > cool_plate
    static const std::vector<std::string> plate_keys = {"hot_plate_temp",           "textured_plate_temp",
                                                        "supertack_plate_temp",     "eng_plate_temp",
                                                        "textured_cool_plate_temp", "cool_plate_temp"};

    const std::string suffix = is_initial_layer ? "_initial_layer" : "";
    const std::string pf_key = is_initial_layer ? "first_layer_bed_temperature" : "bed_temperature";

    std::string selected_plate;
    std::string selected_temp;
    std::vector<std::pair<std::string, std::string>> all_temps; // For lossy report

    for (const auto &plate_key : plate_keys)
    {
        std::string full_key = plate_key + suffix;
        if (!j.contains(full_key))
            continue;

        const auto &val = j[full_key];
        std::string temp_str;
        if (val.is_array() && !val.empty())
        {
            if (val[0].is_string())
                temp_str = val[0].get<std::string>();
            else if (val[0].is_number())
                temp_str = std::to_string(val[0].get<int>());
        }
        else if (val.is_string())
        {
            temp_str = val.get<std::string>();
        }
        else if (val.is_number())
        {
            temp_str = std::to_string(val.get<int>());
        }

        if (!temp_str.empty() && temp_str != "0" && temp_str != "nil")
        {
            all_temps.emplace_back(plate_key, temp_str);
            if (selected_plate.empty())
            {
                selected_plate = plate_key;
                selected_temp = temp_str;
            }
        }
    }

    if (!selected_temp.empty())
    {
        try
        {
            config.set_deserialize_strict(pf_key, selected_temp);
        }
        catch (const std::exception &e)
        {
            BOOST_LOG_TRIVIAL(warning) << "OrcaImporter: Failed to set " << pf_key << "=" << selected_temp << ": "
                                       << e.what();
        }

        // Report lossy mapping if multiple bed types had different temperatures
        if (all_temps.size() > 1)
        {
            std::string detail = profile_name + ": " + pf_key + ": used " + selected_plate + " (" + selected_temp +
                                 "C)";
            for (size_t i = 1; i < all_temps.size(); ++i)
                detail += ", ignored " + all_temps[i].first + " (" + all_temps[i].second + "C)";
            result.lossy_mappings.push_back(detail);
        }
    }
}

// -----------------------------------------------------------------------
// JSON profile parsing + key mapping
// -----------------------------------------------------------------------

int OrcaConfigImporter::parse_and_map_profile(const std::string &json_content, Preset::Type preset_type,
                                              DynamicPrintConfig &out_config, const std::string &profile_name,
                                              ImportResult &result)
{
    int mapped_count = 0;

    try
    {
        auto j = nlohmann::json::parse(json_content);
        const OrcaKeyMapper &mapper = OrcaKeyMapper::instance();

        // GCode field names that need placeholder translation
        static const std::set<std::string> gcode_fields = {
            "machine_start_gcode",
            "machine_end_gcode",
            "before_layer_change_gcode",
            "layer_change_gcode",
            "change_filament_gcode",
            "filament_start_gcode",
            "filament_end_gcode",
            "template_custom_gcode",
            // preFlight names too, in case they appear
            "start_gcode",
            "end_gcode",
            "before_layer_gcode",
            "layer_gcode",
            "toolchange_gcode",
            "start_filament_gcode",
            "end_filament_gcode",
        };

        // Handle bed temperatures specially for filament profiles
        if (preset_type == Preset::TYPE_FILAMENT)
        {
            map_bed_temperatures(j, out_config, false, profile_name, result);
            map_bed_temperatures(j, out_config, true, profile_name, result);

            // Handle shrinkage compensation: Orca uses 100% = no shrinkage, preFlight uses 0% = no compensation.
            // filament_shrink (Orca XY) maps to both filament_shrinkage_compensation_x and _y.
            // filament_shrinkage_compensation_z also needs the same value conversion.
            auto convert_shrinkage = [](const nlohmann::json &val) -> std::string
            {
                std::string csv;
                auto convert_one = [](const std::string &s) -> std::string
                {
                    // Strip trailing '%' if present, parse the number, convert: pf = 100 - orca
                    std::string num_str = s;
                    if (!num_str.empty() && num_str.back() == '%')
                        num_str.pop_back();
                    try
                    {
                        double orca_val = std::stod(num_str);
                        double pf_val = 100.0 - orca_val;
                        // Format with enough precision, trim trailing zeros
                        char buf[64];
                        snprintf(buf, sizeof(buf), "%.4g", pf_val);
                        return std::string(buf);
                    }
                    catch (...)
                    {
                        return "0"; // Fallback: no compensation
                    }
                };

                if (val.is_array())
                {
                    for (size_t i = 0; i < val.size(); ++i)
                    {
                        if (i > 0)
                            csv += ",";
                        if (val[i].is_string())
                            csv += convert_one(val[i].get<std::string>());
                        else if (val[i].is_number())
                            csv += convert_one(std::to_string(val[i].get<double>()));
                        else
                            csv += "0";
                    }
                }
                else if (val.is_string())
                {
                    csv = convert_one(val.get<std::string>());
                }
                else if (val.is_number())
                {
                    csv = convert_one(std::to_string(val.get<double>()));
                }
                return csv;
            };

            if (j.contains("filament_shrink"))
            {
                std::string pf_val = convert_shrinkage(j["filament_shrink"]);
                if (!pf_val.empty())
                {
                    try
                    {
                        out_config.set_deserialize_strict("filament_shrinkage_compensation_x", pf_val);
                        out_config.set_deserialize_strict("filament_shrinkage_compensation_y", pf_val);
                        mapped_count += 2;
                    }
                    catch (const std::exception &e)
                    {
                        BOOST_LOG_TRIVIAL(warning) << "OrcaImporter: Failed to convert filament_shrink in "
                                                   << profile_name << ": " << e.what();
                    }
                }
            }

            if (j.contains("filament_shrinkage_compensation_z"))
            {
                std::string pf_val = convert_shrinkage(j["filament_shrinkage_compensation_z"]);
                if (!pf_val.empty())
                {
                    try
                    {
                        out_config.set_deserialize_strict("filament_shrinkage_compensation_z", pf_val);
                        ++mapped_count;
                    }
                    catch (const std::exception &e)
                    {
                        BOOST_LOG_TRIVIAL(warning)
                            << "OrcaImporter: Failed to convert filament_shrinkage_compensation_z in " << profile_name
                            << ": " << e.what();
                    }
                }
            }
        }

        // Handle printable_area -> bed_shape specially (JSON array of "XxY" strings -> CSV)
        if (preset_type == Preset::TYPE_PRINTER && j.contains("printable_area") && j["printable_area"].is_array())
        {
            std::string bed_shape;
            for (size_t i = 0; i < j["printable_area"].size(); ++i)
            {
                if (i > 0)
                    bed_shape += ",";
                bed_shape += j["printable_area"][i].get<std::string>();
            }
            if (!bed_shape.empty())
            {
                try
                {
                    out_config.set_deserialize_strict("bed_shape", bed_shape);
                    ++mapped_count;
                }
                catch (...)
                {
                }
            }
        }

        // Handle support Z distances specially for print profiles (Orca float mm -> preFlight enum).
        if (preset_type == Preset::TYPE_PRINT)
        {
            auto parse_z_distance = [](const nlohmann::json &val) -> double
            {
                if (val.is_number())
                    return val.get<double>();
                if (val.is_string())
                {
                    try
                    {
                        return std::stod(val.get<std::string>());
                    }
                    catch (...)
                    {
                    }
                }
                if (val.is_array() && !val.empty())
                {
                    if (val[0].is_number())
                        return val[0].get<double>();
                    if (val[0].is_string())
                    {
                        try
                        {
                            return std::stod(val[0].get<std::string>());
                        }
                        catch (...)
                        {
                        }
                    }
                }
                return -1.0; // sentinel: parse failed
            };

            // Top contact: use "custom" enum + set the custom distance float for precise mapping.
            if (j.contains("support_top_z_distance"))
            {
                double dist = parse_z_distance(j["support_top_z_distance"]);
                if (dist >= 0.0)
                {
                    try
                    {
                        if (dist < 0.001)
                        {
                            out_config.set_deserialize_strict("support_material_contact_distance", "no_gap");
                        }
                        else
                        {
                            char dist_buf[64];
                            snprintf(dist_buf, sizeof(dist_buf), "%g", dist);
                            out_config.set_deserialize_strict("support_material_contact_distance", "custom");
                            out_config.set_deserialize_strict("support_material_contact_distance_custom",
                                                              std::string(dist_buf));
                        }
                        ++mapped_count;
                    }
                    catch (const std::exception &e)
                    {
                        BOOST_LOG_TRIVIAL(warning) << "OrcaImporter: Failed to convert support_top_z_distance in "
                                                   << profile_name << ": " << e.what();
                    }
                }
            }

            // Bottom contact: no "custom" option, use thresholds.
            if (j.contains("support_bottom_z_distance"))
            {
                double dist = parse_z_distance(j["support_bottom_z_distance"]);
                if (dist >= 0.0)
                {
                    try
                    {
                        std::string enum_val;
                        if (dist < 0.05)
                            enum_val = "no_gap";
                        else if (dist < 0.15)
                            enum_val = "half_layer";
                        else
                            enum_val = "full_layer";
                        out_config.set_deserialize_strict("support_material_bottom_contact_distance", enum_val);
                        ++mapped_count;
                    }
                    catch (const std::exception &e)
                    {
                        BOOST_LOG_TRIVIAL(warning) << "OrcaImporter: Failed to convert support_bottom_z_distance in "
                                                   << profile_name << ": " << e.what();
                    }
                }
            }
        }

        // Iterate all JSON keys
        for (auto &[key, value] : j.items())
        {
            // Skip metadata and specially-handled keys
            if (key == "name" || key == "inherits" || key == "version" || key == "from" || key == "is_custom_defined" ||
                key == "setting_id" || key == "printable_area")
                continue;

            // Skip bed temperature and shrinkage compensation keys for filaments (handled above)
            if (preset_type == Preset::TYPE_FILAMENT &&
                (key.find("_plate_temp") != std::string::npos || key == "filament_shrink" ||
                 key == "filament_shrinkage_compensation_z"))
                continue;

            // Skip support Z distance keys for print profiles (handled above)
            if (preset_type == Preset::TYPE_PRINT &&
                (key == "support_top_z_distance" || key == "support_bottom_z_distance"))
                continue;

            if (mapper.is_ignored(key, preset_type))
                continue;

            if (mapper.is_orca_only(key, preset_type))
            {
                result.dropped_keys.push_back(key);
                continue;
            }

            // Convert JSON value to string
            std::string str_value;
            if (value.is_string())
            {
                if (value.get<std::string>() == "nil")
                    continue; // "nil" means "use default" - skip entirely
                str_value = value.get<std::string>();
            }
            else if (value.is_array())
            {
                // Check if ALL elements are "nil" - if so, skip the key entirely
                // (means "use printer/system default" in Orca)
                bool all_nil = true;
                for (const auto &elem : value)
                {
                    if (!elem.is_string() || elem.get<std::string>() != "nil")
                    {
                        all_nil = false;
                        break;
                    }
                }
                if (all_nil)
                    continue; // Skip - default config already has correct values

                // G-code arrays need special handling: the strings contain ';' (G-code comments)
                // that deserialize would split on, destroying the G-code. Set directly to
                // bypass the serialize/deserialize roundtrip.
                if (gcode_fields.count(key) > 0)
                {
                    std::vector<std::string> gcode_strings;
                    for (const auto &elem : value)
                    {
                        if (elem.is_string() && elem.get<std::string>() != "nil")
                            gcode_strings.push_back(elem.get<std::string>());
                        else
                            gcode_strings.push_back("");
                    }

                    // Translate Orca placeholders to preFlight format
                    for (auto &s : gcode_strings)
                        s = translate_gcode(s, profile_name, key, result.gcode_warnings);

                    // Map the key name (e.g. filament_start_gcode -> start_filament_gcode)
                    auto [pf_key, pf_val_unused] = mapper.map_key_value(key, "", preset_type);
                    if (pf_key.empty())
                    {
                        result.dropped_keys.push_back(key);
                        continue;
                    }

                    try
                    {
                        // Filament G-code fields (start_filament_gcode, end_filament_gcode) are
                        // coStrings (one entry per extruder). Printer-level G-code fields
                        // (start_gcode, end_gcode, etc.) are coString (singular).
                        const ConfigOptionDef *opt_def = print_config_def.get(pf_key);
                        if (opt_def && opt_def->type == coStrings)
                            out_config.set_key_value(pf_key, new ConfigOptionStrings(std::move(gcode_strings)));
                        else
                            out_config.set_key_value(pf_key, new ConfigOptionString(boost::join(gcode_strings, "\n")));
                        ++mapped_count;
                    }
                    catch (const std::exception &e)
                    {
                        BOOST_LOG_TRIVIAL(warning)
                            << "OrcaImporter: Failed to set " << pf_key << " in " << profile_name << ": " << e.what();
                    }
                    continue;
                }

                // Any array targeting a ConfigOptionStrings option must be set directly.
                // The generic comma-separated path below feeds into set_deserialize_strict,
                // which calls unescape_strings_cstyle - that splits on ';' and interprets '\'
                // as escape sequences, corrupting values that contain those characters.
                {
                    auto [pf_key_peek, unused] = mapper.map_key_value(key, "", preset_type);
                    const ConfigOptionDef *opt_def = pf_key_peek.empty() ? nullptr : print_config_def.get(pf_key_peek);
                    if (opt_def && opt_def->type == coStrings)
                    {
                        std::vector<std::string> strings;
                        for (const auto &elem : value)
                        {
                            if (elem.is_string())
                            {
                                std::string s = elem.get<std::string>();
                                if (s != "nil")
                                    strings.push_back(std::move(s));
                                else
                                    strings.push_back("");
                            }
                            else if (elem.is_number_float())
                                strings.push_back(std::to_string(elem.get<double>()));
                            else if (elem.is_number_integer())
                                strings.push_back(std::to_string(elem.get<int>()));
                            else
                                strings.push_back("");
                        }

                        try
                        {
                            out_config.set_key_value(pf_key_peek, new ConfigOptionStrings(std::move(strings)));
                            ++mapped_count;
                        }
                        catch (const std::exception &e)
                        {
                            BOOST_LOG_TRIVIAL(warning) << "OrcaImporter: Failed to set " << pf_key_peek << " in "
                                                       << profile_name << ": " << e.what();
                        }
                        continue;
                    }
                }

                // Convert array to comma-separated values (preFlight format for multi-value options)
                for (size_t i = 0; i < value.size(); ++i)
                {
                    std::string elem_str;
                    if (value[i].is_string())
                    {
                        std::string elem = value[i].get<std::string>();
                        // If a single element in a mixed array is "nil", use empty
                        // (for nullable options like ConfigOptionFloatsNullable)
                        if (elem != "nil")
                            elem_str = elem;
                    }
                    else if (value[i].is_number_float())
                    {
                        elem_str = std::to_string(value[i].get<double>());
                    }
                    else if (value[i].is_number_integer())
                    {
                        elem_str = std::to_string(value[i].get<int>());
                    }
                    else if (value[i].is_boolean())
                    {
                        elem_str = value[i].get<bool>() ? "1" : "0";
                    }
                    if (!str_value.empty())
                        str_value += ",";
                    str_value += elem_str;
                }
            }
            else if (value.is_number_float())
            {
                str_value = std::to_string(value.get<double>());
            }
            else if (value.is_number_integer())
            {
                str_value = std::to_string(value.get<int>());
            }
            else if (value.is_boolean())
            {
                str_value = value.get<bool>() ? "1" : "0";
            }
            else
            {
                continue; // Skip objects, nulls, etc.
            }

            // Translate GCode placeholders in GCode fields
            bool is_gcode_field = gcode_fields.count(key) > 0;
            if (is_gcode_field)
                str_value = translate_gcode(str_value, profile_name, key, result.gcode_warnings);

            // Map through the key mapper
            auto [pf_key, pf_value] = mapper.map_key_value(key, str_value, preset_type);

            if (pf_key.empty())
            {
                // Unknown key not in our mapping table at all - treat as dropped
                result.dropped_keys.push_back(key);
                continue;
            }

            // Apply GCode translation to the mapped value too if it's a renamed gcode field
            if (!is_gcode_field)
            {
                static const std::set<std::string> pf_gcode_keys = {
                    "start_gcode",        "end_gcode",
                    "before_layer_gcode", "layer_gcode",
                    "toolchange_gcode",   "start_filament_gcode",
                    "end_filament_gcode", "template_custom_gcode",
                };
                if (pf_gcode_keys.count(pf_key) > 0)
                    pf_value = translate_gcode(pf_value, profile_name, pf_key, result.gcode_warnings);
            }

            // Try to set the value in the config
            try
            {
                out_config.set_deserialize_strict(pf_key, pf_value);
                ++mapped_count;
            }
            catch (const std::exception &e)
            {
                BOOST_LOG_TRIVIAL(warning) << "OrcaImporter: Failed to set " << pf_key << "=" << pf_value << " in "
                                           << profile_name << ": " << e.what();
            }
        }

        // Resolve acceleration percentages: Orca allows values like "50%" meaning
        // that percentage of default_acceleration. preFlight needs absolute mm/s^2.
        if (preset_type == Preset::TYPE_PRINT)
        {
            // Orca key -> preFlight key
            static const std::vector<std::pair<std::string, std::string>> accel_pct_keys = {
                {"bridge_acceleration", "bridge_acceleration"},
                {"sparse_infill_acceleration", "infill_acceleration"},
                {"internal_solid_infill_acceleration", "solid_infill_acceleration"},
                {"top_surface_acceleration", "top_solid_infill_acceleration"},
                {"initial_layer_acceleration", "first_layer_acceleration"},
                {"inner_wall_acceleration", "perimeter_acceleration"},
                {"outer_wall_acceleration", "external_perimeter_acceleration"},
                {"travel_acceleration", "travel_acceleration"},
            };

            double default_accel = 0.0;
            auto *opt = out_config.option<ConfigOptionFloat>("default_acceleration");
            if (opt)
                default_accel = opt->value;

            for (auto &[orca_key, pf_key] : accel_pct_keys)
            {
                if (!j.contains(orca_key))
                    continue;

                std::string raw;
                if (j[orca_key].is_string())
                    raw = j[orca_key].get<std::string>();
                else
                    continue;

                if (raw.empty() || raw.back() != '%')
                    continue;

                raw.pop_back(); // strip '%'
                try
                {
                    double pct = std::stod(raw);
                    double absolute = default_accel * pct / 100.0;
                    // Round to integer - acceleration values are whole numbers
                    int accel_int = static_cast<int>(std::round(absolute));
                    out_config.set_deserialize_strict(pf_key, std::to_string(accel_int));
                    ++mapped_count;
                }
                catch (const std::exception &e)
                {
                    BOOST_LOG_TRIVIAL(warning) << "OrcaImporter: Failed to resolve " << orca_key << " percentage in "
                                               << profile_name << ": " << e.what();
                }
            }
        }
    }
    catch (const nlohmann::json::parse_error &e)
    {
        result.errors.push_back("JSON parse error in " + profile_name + ": " + std::string(e.what()));
    }
    catch (const std::exception &e)
    {
        result.errors.push_back("Error processing " + profile_name + ": " + std::string(e.what()));
    }

    return mapped_count;
}

// -----------------------------------------------------------------------
// Inheritance resolution
// -----------------------------------------------------------------------

void OrcaConfigImporter::resolve_inheritance(DynamicPrintConfig &config, const std::string &inherits,
                                             Preset::Type preset_type, PresetBundle &bundle, ImportResult &result,
                                             const std::set<std::string> *explicit_keys)
{
    if (inherits.empty())
        return;

    // Helper: apply parent values for keys the child didn't explicitly set.
    auto apply_parent = [&](const DynamicPrintConfig &parent_config)
    {
        for (const auto &key : parent_config.keys())
        {
            if (explicit_keys && explicit_keys->count(key))
                continue; // Child explicitly set this key, don't override
            config.set_key_value(key, parent_config.option(key)->clone());
        }
    };

    // Step 1: Check pending profiles from this bundle
    auto it = m_pending_profiles.find(inherits);
    if (it != m_pending_profiles.end())
    {
        apply_parent(it->second);
        return;
    }

    // Step 2: Check preFlight existing presets
    PresetCollection *collection = nullptr;
    switch (preset_type)
    {
    case Preset::TYPE_PRINTER:
        collection = &bundle.printers;
        break;
    case Preset::TYPE_FILAMENT:
        collection = &bundle.filaments;
        break;
    case Preset::TYPE_PRINT:
        collection = &bundle.prints;
        break;
    default:
        return;
    }

    const Preset *parent = collection->find_preset(inherits, false);
    if (parent)
    {
        apply_parent(parent->config);
        return;
    }

    // Step 2b: Try bundled Orca system profile defaults
    if (load_orca_system_defaults(inherits, preset_type, config, explicit_keys))
        return;

    // Step 3: Parent not found - warn and fall back to preFlight defaults
    BOOST_LOG_TRIVIAL(warning) << "OrcaImporter: Could not resolve inheritance from '" << inherits
                               << "'. Using defaults.";
    result.unresolved_inheritance.push_back(inherits);
}

// -----------------------------------------------------------------------
// Orca system profile loading
// -----------------------------------------------------------------------

bool OrcaConfigImporter::load_orca_system_defaults(const std::string &parent_name, Preset::Type type,
                                                   DynamicPrintConfig &config,
                                                   const std::set<std::string> *explicit_keys)
{
    // Only filament profiles have bundled Orca system defaults
    if (type != Preset::TYPE_FILAMENT)
        return false;

    // Lazy-load the system profiles JSON
    if (!m_orca_system_loaded)
    {
        m_orca_system_loaded = true;
        std::string path = resources_dir() + "/profiles/orca_system_profiles.json";
        boost::nowide::ifstream ifs(path);
        if (ifs)
        {
            try
            {
                m_orca_system_profiles = nlohmann::json::parse(ifs);
            }
            catch (const std::exception &e)
            {
                BOOST_LOG_TRIVIAL(error) << "OrcaImporter: Failed to parse " << path << ": " << e.what();
            }
        }
        else
        {
            BOOST_LOG_TRIVIAL(warning) << "OrcaImporter: Orca system profiles not found at " << path;
        }
    }

    if (m_orca_system_profiles.empty() || !m_orca_system_profiles.contains(parent_name))
        return false;

    // Build the inheritance chain within the system profiles, from child up to root
    std::vector<std::string> chain;
    std::string current = parent_name;
    while (!current.empty() && m_orca_system_profiles.contains(current) && chain.size() < 10)
    {
        chain.push_back(current);
        current = m_orca_system_profiles[current].value("inherits", "");
    }

    // Merge from root to child (so child values override parent)
    nlohmann::json merged;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
    {
        for (auto &[key, value] : m_orca_system_profiles[*it].items())
        {
            if (key != "inherits") // Don't carry inherits into the merged result
                merged[key] = value;
        }
    }

    // Translate Orca keys to preFlight keys via the existing parser
    DynamicPrintConfig system_config;
    ImportResult dummy_result;
    std::string merged_json = merged.dump();
    parse_and_map_profile(merged_json, type, system_config, parent_name, dummy_result);

    // Apply system profile values for keys the child didn't explicitly set
    for (const auto &key : system_config.keys())
    {
        if (explicit_keys && explicit_keys->count(key))
            continue;
        config.set_key_value(key, system_config.option(key)->clone());
    }

    BOOST_LOG_TRIVIAL(info) << "OrcaImporter: Resolved '" << parent_name << "' from bundled Orca system profiles ("
                            << system_config.keys().size() << " keys)";
    return true;
}

// -----------------------------------------------------------------------
// Saving a single preset
// -----------------------------------------------------------------------

std::string OrcaConfigImporter::save_preset(DynamicPrintConfig &config, const std::string &profile_name,
                                            Preset::Type preset_type, PresetBundle &bundle,
                                            std::function<int(const std::string &)> &confirm_overwrite,
                                            ImportResult &result)
{
    PresetCollection *collection = nullptr;
    switch (preset_type)
    {
    case Preset::TYPE_PRINTER:
        collection = &bundle.printers;
        break;
    case Preset::TYPE_FILAMENT:
        collection = &bundle.filaments;
        break;
    case Preset::TYPE_PRINT:
        collection = &bundle.prints;
        break;
    default:
        result.errors.push_back("Unknown preset type for '" + profile_name + "'");
        return {};
    }

    // Check for existing preset with the same name
    const Preset *existing = collection->find_preset(profile_name, false);
    if (existing)
    {
        if (existing->is_system)
        {
            // Never overwrite system presets
            result.errors.push_back("Skipped '" + profile_name + "': system preset cannot be overwritten");
            return {};
        }

        if (confirm_overwrite)
        {
            int answer = confirm_overwrite(profile_name);
            if (answer < 0) // Cancel
            {
                result.errors.push_back("Import cancelled by user");
                return {};
            }
            if (answer == 0) // Skip
                return {};
        }
    }

    // Normalize and clean up invalid keys
    Preset::normalize(config);

    const DynamicPrintConfig *default_config = nullptr;
    if (preset_type == Preset::TYPE_PRINTER)
        default_config = &collection->default_preset_for(config).config;
    else
        default_config = &collection->default_preset().config;

    Preset::remove_invalid_keys(config, *default_config);

    // Build file path the same way load_configbundle does
    auto file_name = profile_name + ".ini";
    auto file_path = (boost::filesystem::path(data_dir()) / collection->section_name() / file_name).make_preferred();

    // Load into the collection and save to disk
    Preset &loaded = collection->load_preset(file_path.string(), profile_name, std::move(config), false);

    // Filaments are independent of printers - strip any Orca printer-coupling data
    if (preset_type == Preset::TYPE_FILAMENT)
    {
        if (loaded.config.has("compatible_printers"))
            loaded.config.option<ConfigOptionStrings>("compatible_printers")->values.clear();
        if (loaded.config.has("compatible_printers_condition"))
            loaded.config.option<ConfigOptionString>("compatible_printers_condition")->value.clear();
    }

    loaded.save();

    return profile_name;
}

// -----------------------------------------------------------------------
// Main import entry point
// -----------------------------------------------------------------------

OrcaConfigImporter::ImportResult OrcaConfigImporter::import_bundle(
    const std::string &zip_path, PresetBundle &preset_bundle, const ImportOptions &options,
    std::function<int(const std::string &)> confirm_overwrite)
{
    ImportResult result;
    m_pending_profiles.clear();
    m_pending_types.clear();
    m_pending_explicit_keys.clear();
    m_orca_system_loaded = false;
    m_orca_system_profiles = nlohmann::json();

    // Open the ZIP archive
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!open_zip_reader(&zip, zip_path))
    {
        result.errors.push_back("Failed to open ZIP archive: " + zip_path);
        return result;
    }

    // Read and parse manifest
    std::string manifest_json = read_zip_entry(&zip, "bundle_structure.json");
    if (manifest_json.empty())
    {
        result.errors.push_back("No bundle_structure.json found in archive");
        close_zip_reader(&zip);
        return result;
    }

    BundleManifest manifest = parse_manifest(manifest_json);

    // Phase 1: Read all profiles into m_pending_profiles (for inheritance resolution)
    auto load_profiles = [&](const std::vector<std::string> &paths, Preset::Type type)
    {
        for (const auto &path : paths)
        {
            std::string json_str = read_zip_entry(&zip, path);
            if (json_str.empty())
            {
                result.errors.push_back("Failed to read " + path + " from archive");
                continue;
            }

            // Extract name from JSON
            try
            {
                auto j = nlohmann::json::parse(json_str);
                std::string name = j.value("name", "");
                if (!name.empty())
                {
                    // Initialize config from defaults so vector options have proper sizes.
                    // This matches how load_configbundle works.
                    PresetCollection *collection = nullptr;
                    switch (type)
                    {
                    case Preset::TYPE_PRINTER:
                        collection = &preset_bundle.printers;
                        break;
                    case Preset::TYPE_FILAMENT:
                        collection = &preset_bundle.filaments;
                        break;
                    case Preset::TYPE_PRINT:
                        collection = &preset_bundle.prints;
                        break;
                    default:
                        continue;
                    }
                    const DynamicPrintConfig &default_config = collection->default_preset().config;
                    DynamicPrintConfig config = default_config;
                    parse_and_map_profile(json_str, type, config, name, result);

                    // Track which keys the Orca JSON explicitly set (differ from defaults)
                    std::set<std::string> explicit_keys;
                    for (const auto &key : config.keys())
                    {
                        const ConfigOption *opt = config.option(key);
                        const ConfigOption *def_opt = default_config.option(key);
                        if (!def_opt || *opt != *def_opt)
                            explicit_keys.insert(key);
                    }
                    m_pending_explicit_keys[name] = std::move(explicit_keys);

                    m_pending_profiles[name] = std::move(config);
                    m_pending_types[name] = type;

                    // Store inherits info for later resolution
                    std::string inherits_val = j.value("inherits", "");
                    if (!inherits_val.empty())
                    {
                        // We store the raw JSON so we can re-extract inherits during resolution
                        // Actually, we need to track the inherits value separately
                        m_pending_profiles[name].set_key_value("_orca_inherits", new ConfigOptionString(inherits_val));
                    }
                }
            }
            catch (const std::exception &e)
            {
                result.errors.push_back("Failed to parse profile from " + path + ": " + std::string(e.what()));
            }
        }
    };

    if (options.import_printer)
        load_profiles(manifest.printer_configs, Preset::TYPE_PRINTER);
    if (options.import_filaments)
        load_profiles(manifest.filament_configs, Preset::TYPE_FILAMENT);
    if (options.import_processes)
        load_profiles(manifest.process_configs, Preset::TYPE_PRINT);

    close_zip_reader(&zip);

    // Phase 2: Resolve inheritance and save each profile.
    // Each iteration is wrapped in try-catch so a single profile failure
    // (e.g. filesystem error during save) doesn't abort the entire import
    // and bypass the results dialog.
    for (auto &[name, config] : m_pending_profiles)
    {
        try
        {
            auto type_it = m_pending_types.find(name);
            if (type_it == m_pending_types.end())
                continue;

            Preset::Type type = type_it->second;

            // Check if user wanted this type
            if ((type == Preset::TYPE_PRINTER && !options.import_printer) ||
                (type == Preset::TYPE_FILAMENT && !options.import_filaments) ||
                (type == Preset::TYPE_PRINT && !options.import_processes))
                continue;

            // Resolve inheritance
            auto *inherits_opt = config.opt<ConfigOptionString>("_orca_inherits");
            if (inherits_opt && !inherits_opt->value.empty())
            {
                auto ek_it = m_pending_explicit_keys.find(name);
                const std::set<std::string> *expl_keys = (ek_it != m_pending_explicit_keys.end()) ? &ek_it->second
                                                                                                  : nullptr;
                resolve_inheritance(config, inherits_opt->value, type, preset_bundle, result, expl_keys);
            }

            // Remove our internal tracking key
            config.erase("_orca_inherits");

            // Apply default config as base for any missing keys
            PresetCollection *collection = nullptr;
            switch (type)
            {
            case Preset::TYPE_PRINTER:
                collection = &preset_bundle.printers;
                break;
            case Preset::TYPE_FILAMENT:
                collection = &preset_bundle.filaments;
                break;
            case Preset::TYPE_PRINT:
                collection = &preset_bundle.prints;
                break;
            default:
                continue;
            }

            const DynamicPrintConfig *default_config = nullptr;
            if (type == Preset::TYPE_PRINTER)
                default_config = &collection->default_preset_for(config).config;
            else
                default_config = &collection->default_preset().config;

            // Fill in defaults for keys not yet set
            DynamicPrintConfig full_config = *default_config;
            full_config.apply(config);

            // Save the preset
            std::string saved_name = save_preset(full_config, name, type, preset_bundle, confirm_overwrite, result);
            if (saved_name.empty())
                continue;

            switch (type)
            {
            case Preset::TYPE_PRINTER:
                result.imported_printers.push_back(saved_name);
                break;
            case Preset::TYPE_FILAMENT:
                result.imported_filaments.push_back(saved_name);
                break;
            case Preset::TYPE_PRINT:
                result.imported_prints.push_back(saved_name);
                break;
            default:
                break;
            }
        }
        catch (const std::exception &e)
        {
            result.errors.push_back("Failed to import '" + name + "': " + std::string(e.what()));
        }
    }

    // Count mapped settings
    // (We track per-type counts during parse_and_map_profile; for now use profile counts)
    result.printer_settings_count = (int) result.imported_printers.size();
    result.filament_settings_count = (int) result.imported_filaments.size();
    result.process_settings_count = (int) result.imported_prints.size();

    // Deduplicate dropped keys
    std::sort(result.dropped_keys.begin(), result.dropped_keys.end());
    result.dropped_keys.erase(std::unique(result.dropped_keys.begin(), result.dropped_keys.end()),
                              result.dropped_keys.end());

    result.success = result.errors.empty() || (!result.imported_printers.empty() ||
                                               !result.imported_filaments.empty() || !result.imported_prints.empty());

    return result;
}

} // namespace Luminary
