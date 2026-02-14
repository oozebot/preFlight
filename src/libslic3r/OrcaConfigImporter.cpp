///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "OrcaConfigImporter.hpp"
#include "OrcaKeyMapping.hpp"
#include "PresetBundle.hpp"
#include "Utils.hpp"
#include "miniz_extension.hpp"

#include <regex>
#include <set>

#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>

#include <miniz.h>
#include "nlohmann/json.hpp"

#include "libslic3r.h"

namespace Slic3r
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

    // Step 1: Direct placeholder mappings (specific Orca -> preFlight)
    static const std::vector<std::pair<std::string, std::string>> direct_mappings = {
        {"[nozzle_temperature_initial_layer]", "{first_layer_temperature[0]}"},
        {"[nozzle_temperature]", "{temperature[0]}"},
        {"[bed_temperature_initial_layer_single]", "{first_layer_bed_temperature[0]}"},
        {"[bed_temperature_initial_layer]", "{first_layer_bed_temperature}"},
        {"[bed_temperature]", "{bed_temperature}"},
        {"[chamber_temperature]", "{chamber_temperature}"},
        {"[overall_chamber_temperature]", "{chamber_temperature}"},
        {"[layer_z]", "{layer_z}"},
        {"[layer_num]", "{layer_num}"},
        {"[max_layer_z]", "{max_layer_z}"},
        {"[total_layer_count]", "{total_layer_count}"},
        {"[previous_extruder]", "{previous_extruder}"},
        {"[next_extruder]", "{next_extruder}"},
        {"[current_extruder]", "{current_extruder}"},
        {"[initial_extruder]", "{initial_extruder}"},
        {"[toolchange_z]", "{toolchange_z}"},
        {"[print_time]", "{print_time}"},
        {"[total_weight]", "{total_weight}"},
        {"[total_cost]", "{total_cost}"},
        {"[input_filename_base]", "{input_filename_base}"},
        {"[filament_type]", "{filament_type[0]}"},
    };

    for (const auto &[orca, pf] : direct_mappings)
        boost::replace_all(result, orca, pf);

    // Step 2: Handle array access patterns: [key[index]] -> {key[index]}
    try
    {
        std::regex array_pattern(R"(\[([a-z_]+)\[([^\]]+)\]\])");
        result = std::regex_replace(result, array_pattern, "{$1[$2]}");
    }
    catch (...)
    {
    }

    // Step 3: Convert remaining simple [placeholder] -> {placeholder}
    try
    {
        std::regex simple_pattern(R"(\[([a-z_][a-z_0-9]*)\])");
        result = std::regex_replace(result, simple_pattern, "{$1}");
    }
    catch (...)
    {
    }

    // Step 4: Warn about Orca-specific placeholders that have no preFlight equivalent
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
        std::string search = "{" + ph + "}";
        if (result.find(search) != std::string::npos)
        {
            warnings.push_back(profile_name + ": Orca-only placeholder {" + ph + "} in " + field_name +
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

        // Iterate all JSON keys
        for (auto &[key, value] : j.items())
        {
            // Skip metadata and specially-handled keys
            if (key == "name" || key == "inherits" || key == "version" || key == "from" || key == "is_custom_defined" ||
                key == "setting_id" || key == "printable_area")
                continue;

            // Skip bed temperature keys for filaments (handled above)
            if (preset_type == Preset::TYPE_FILAMENT && (key.find("_plate_temp") != std::string::npos))
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
                                             Preset::Type preset_type, PresetBundle &bundle)
{
    if (inherits.empty())
        return;

    // Step 1: Check pending profiles from this bundle
    auto it = m_pending_profiles.find(inherits);
    if (it != m_pending_profiles.end())
    {
        // Apply parent values as defaults (only for keys not already set)
        for (const auto &key : it->second.keys())
        {
            if (!config.has(key))
                config.set_key_value(key, it->second.option(key)->clone());
        }
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
        for (const auto &key : parent->config.keys())
        {
            if (!config.has(key))
                config.set_key_value(key, parent->config.option(key)->clone());
        }
        return;
    }

    // Step 3: Parent not found - apply defaults and log warning
    BOOST_LOG_TRIVIAL(warning) << "OrcaImporter: Could not resolve inheritance from '" << inherits
                               << "'. Using defaults.";

    const Preset &default_preset = collection->default_preset_for(config);
    for (const auto &key : default_preset.config.keys())
    {
        if (!config.has(key))
            config.set_key_value(key, default_preset.config.option(key)->clone());
    }
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
                    DynamicPrintConfig config = collection->default_preset().config;
                    parse_and_map_profile(json_str, type, config, name, result);
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

    // Phase 2: Resolve inheritance and save each profile
    for (auto &[name, config] : m_pending_profiles)
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
            resolve_inheritance(config, inherits_opt->value, type, preset_bundle);
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

} // namespace Slic3r
