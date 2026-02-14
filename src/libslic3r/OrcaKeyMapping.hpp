///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#ifndef slic3r_OrcaKeyMapping_hpp_
#define slic3r_OrcaKeyMapping_hpp_

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "Preset.hpp"

namespace Slic3r
{

// Describes how a single OrcaSlicer key maps to a preFlight key.
struct OrcaKeyEntry
{
    enum class MapType
    {
        Direct,      // Same key name, compatible value format
        Renamed,     // Different key name, same value format
        Transformed, // Different key name and/or value format (needs transform function)
        OrcaOnly,    // No preFlight equivalent - will be reported as dropped
        Ignored      // Metadata key - silently skip (name, version, from, etc.)
    };

    std::string orca_key;
    std::string preflight_key; // Empty for OrcaOnly / Ignored
    MapType type;
    // Optional value transform; nullptr means pass value through unchanged.
    std::function<std::string(const std::string &)> transform;
};

// Singleton that holds all OrcaSlicer -> preFlight key mapping tables
// and value transformation functions.
class OrcaKeyMapper
{
public:
    static OrcaKeyMapper &instance();

    // Look up the preFlight key and transformed value for an Orca key.
    // Returns {"", ""} if the key is OrcaOnly or Ignored.
    std::pair<std::string, std::string> map_key_value(const std::string &orca_key, const std::string &orca_value,
                                                      Preset::Type preset_type) const;

    // Returns true if the key should be silently skipped (Ignored type).
    bool is_ignored(const std::string &orca_key, Preset::Type preset_type) const;

    // Returns true if the key is Orca-only (no preFlight equivalent).
    bool is_orca_only(const std::string &orca_key, Preset::Type preset_type) const;

    // Get all Orca-only key names for a preset type (for the "dropped" report).
    std::vector<std::string> get_orca_only_keys(Preset::Type preset_type) const;

    // --- Value transformation helpers (public for testing) ---
    static std::string transform_infill_pattern(const std::string &value);
    static std::string transform_seam_position(const std::string &value);
    static std::string transform_support_style(const std::string &value);
    static std::string transform_brim_type(const std::string &value);
    static std::string transform_wall_sequence_to_bool(const std::string &value);
    static std::string transform_print_sequence_to_bool(const std::string &value);
    static std::string transform_only_one_wall_top(const std::string &value);
    static std::string transform_emit_machine_limits(const std::string &value);
    static std::string transform_wall_generator(const std::string &value);
    static std::string transform_gcode_label_objects(const std::string &value);
    static std::string transform_fuzzy_skin(const std::string &value);

private:
    OrcaKeyMapper();
    void build_printer_mappings();
    void build_filament_mappings();
    void build_process_mappings();

    // Look up entry; returns nullptr if not found.
    const OrcaKeyEntry *find_entry(const std::string &orca_key, Preset::Type preset_type) const;

    // Preset::Type -> list of mapping entries
    std::map<Preset::Type, std::vector<OrcaKeyEntry>> m_mappings;
};

} // namespace Slic3r

#endif // slic3r_OrcaKeyMapping_hpp_
