///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#ifndef slic3r_OrcaConfigImporter_hpp_
#define slic3r_OrcaConfigImporter_hpp_

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "Preset.hpp"
#include "PrintConfig.hpp"

namespace Slic3r
{

class PresetBundle;

class OrcaConfigImporter
{
public:
    // What the user chose to import.
    struct ImportOptions
    {
        bool import_printer = true;
        bool import_filaments = true;
        bool import_processes = true;
    };

    // Full result of an import operation.
    struct ImportResult
    {
        bool success = false;

        // Successfully imported profile names
        std::vector<std::string> imported_printers;
        std::vector<std::string> imported_filaments;
        std::vector<std::string> imported_prints;

        // Counts of cleanly-mapped settings per profile type
        int printer_settings_count = 0;
        int filament_settings_count = 0;
        int process_settings_count = 0;

        // Lossy mappings - imported but with data loss
        std::vector<std::string> lossy_mappings;

        // Orca-only keys that were dropped
        std::vector<std::string> dropped_keys;

        // GCode placeholders that couldn't be fully translated
        std::vector<std::string> gcode_warnings;

        // Fatal errors (corrupt ZIP, invalid JSON, etc.)
        std::vector<std::string> errors;
    };

    // Parsed manifest from bundle_structure.json
    struct BundleManifest
    {
        std::string bundle_id;
        std::string bundle_type;
        std::vector<std::string> printer_configs;
        std::vector<std::string> filament_configs;
        std::vector<std::string> process_configs;
        std::string printer_preset_name;
        std::string version;
    };

    // Import from .orca_printer or .orca_filament file.
    // confirm_overwrite is called when a preset with the same name already exists;
    // it should return wxID_YES / wxID_NO / wxID_CANCEL (or 0 to skip, -1 to abort).
    ImportResult import_bundle(const std::string &zip_path, PresetBundle &preset_bundle, const ImportOptions &options,
                               std::function<int(const std::string &)> confirm_overwrite);

    // Parse a manifest from raw JSON (useful for preview before import).
    static BundleManifest parse_manifest(const std::string &json_content);

    // Translate GCode placeholders from Orca [bracket] to preFlight {brace} syntax.
    // Populates warnings vector with untranslatable placeholders.
    static std::string translate_gcode(const std::string &orca_gcode, const std::string &profile_name,
                                       const std::string &field_name, std::vector<std::string> &warnings);

private:
    // Read a file from the ZIP archive into a string. Returns empty on failure.
    static std::string read_zip_entry(void *zip_archive, const std::string &entry_name);

    // Parse one JSON profile and produce a DynamicPrintConfig with preFlight keys.
    // Returns the number of cleanly-mapped settings.
    int parse_and_map_profile(const std::string &json_content, Preset::Type preset_type, DynamicPrintConfig &out_config,
                              const std::string &profile_name, ImportResult &result);

    // Handle the special lossy bed-temperature mapping for filament profiles.
    void map_bed_temperatures(const nlohmann::json &j, DynamicPrintConfig &config, bool is_initial_layer,
                              const std::string &profile_name, ImportResult &result);

    // Resolve inheritance: apply parent values as defaults for missing keys.
    void resolve_inheritance(DynamicPrintConfig &config, const std::string &inherits, Preset::Type preset_type,
                             PresetBundle &bundle);

    // Save a single mapped profile into the preset bundle, returning the preset name.
    std::string save_preset(DynamicPrintConfig &config, const std::string &profile_name, Preset::Type preset_type,
                            PresetBundle &bundle, std::function<int(const std::string &)> &confirm_overwrite,
                            ImportResult &result);

    // Profiles loaded from this bundle, keyed by name (for intra-bundle inheritance).
    std::map<std::string, DynamicPrintConfig> m_pending_profiles;
    // Track which pending profiles are which type.
    std::map<std::string, Preset::Type> m_pending_types;
};

} // namespace Slic3r

#endif // slic3r_OrcaConfigImporter_hpp_
