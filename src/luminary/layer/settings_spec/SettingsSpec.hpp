///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
///|/ Provisional module: the settings specification belongs beside config by subject, and sits in layer because it reads the key invalidation lists from Print.hpp.
///|/
#pragma once

#include "luminary/presets/preset/Preset.hpp"

#include <iosfwd>
#include <string>
#include <vector>

namespace Luminary
{

// The row specification: where every settable key is shown. One table, toolkit-free, beside the
// definition it references by key. The Settings pages and the sidebar render from it; the
// override panel filters it by the keys an object may carry. Strings are the untranslated
// English wrapped in L() so gettext extracts them; a renderer translates when it builds.
enum class SettingWidget
{
    Default,  // the widget the definition's type implies
    Nullable, // the enable-checkbox row of the Filament overrides
    Custom,   // a builder that stays code, named by `custom`
};

// Which preset lists a key belongs to; the Dependencies keys sit in more than one.
enum SettingPreset : unsigned
{
    SettingPresetPrint = 1,
    SettingPresetFilament = 2,
    SettingPresetPrinter = 4,
};
unsigned setting_preset_bit(Preset::Type type);

struct SettingRow
{
    const char *key;           // exists in print_config_def
    unsigned presets;          // SettingPreset bits: the preset lists the key belongs to
    const char *page;          // Settings page title (L), nullptr when the sidebar alone shows the row
    const char *group;         // group title (L), one for the Settings page and the sidebar alike
    int order;                 // build order within the page
    const char *line;          // the composite line's label (L) when several rows share one line
    const char *sidebar_page;  // sidebar page id, nullptr when the Settings page alone shows the row
    const char *sidebar_label; // the sidebar's label (L) when it differs from the definition's
    int sidebar_order;         // build order within the sidebar panel, -1 when not shown there
    SettingWidget widget;
    const char *custom;               // builder id for Custom rows
    bool full_width;                  // the sidebar's full-width row hint
    bool extruder_indexed;            // repeated per extruder on the Printer surfaces
    const char *doc_path;             // the documentation anchor the Settings pages carry
    const char *toggle_rule{nullptr}; // the rule that enables the row, nullptr when it is always enabled
};

// What a rule sees: the preset's edited config, the extruder the row belongs to (-1 for a row
// that is not per extruder) and the printer's extruder count.
struct ToggleContext
{
    const DynamicPrintConfig &config;
    int extruder;
    size_t extruders_count;
};

// A rule that enables the rows naming it. The reason is shown as the disabled row's tooltip,
// written the way the definition's tooltips are. A per-extruder rule is evaluated once per
// extruder and reaches that extruder's row.
struct ToggleRule
{
    const char *id;
    bool per_extruder;
    bool (*enabled)(const ToggleContext &);
    const char *reason; // (L)
};

// The state a rule produced for one row: the key, the extruder (-1 when the rule is not per
// extruder; a surface then applies it to every field of the key) and, when disabled, the reason.
struct ToggleState
{
    std::string key;
    int extruder;
    bool enabled;
    const char *reason; // (L), nullptr when enabled
};

// A Settings page, in the order the tabs show them.
struct SettingPage
{
    unsigned presets;  // SettingPreset bits
    const char *title; // page title (L); the rows name it in `page`
    const char *icon;  // icon resource name
};

// A composite line: several rows on one line under one label, with the line's own tooltip.
struct SettingLine
{
    unsigned presets;
    const char *page;    // (L)
    const char *group;   // (L)
    const char *label;   // (L); the rows name it in `line`
    const char *tooltip; // (L), empty when the line has none
};

// A settable key that deliberately has no row, with the reason.
struct NoUiKey
{
    const char *key;
    unsigned presets;   // SettingPreset bits
    const char *reason; // for the check's report, not shown in the UI
};

// The name of a definition's scope reason (ScopeReason in Config.hpp) for the check's report.
const char *scope_reason_name(ScopeReason reason);

// Where an override may sit: the object carries the object and region keys, a part, modifier
// or layer range the region keys only.
enum class OverrideScope
{
    Object,
    Part,
    Modifier,
    LayerRange,
};
bool overridable_at(OverrideScope scope, const std::string &key);

const std::vector<SettingRow> &setting_rows();
const std::vector<SettingPage> &setting_pages();
const std::vector<SettingLine> &setting_lines();
const std::vector<NoUiKey> &no_ui_keys();
const std::vector<ToggleRule> &toggle_rules();
const SettingRow *find_setting_row(const std::string &key);
const SettingLine *find_setting_line(const SettingRow &row);
const ToggleRule *find_toggle_rule(const std::string &id);

// The keys of one preset list, generated from the rows with the preset's bit (in spec order)
// and the no-UI keys with it; Preset::print_options and its siblings return these.
std::vector<std::string> setting_preset_keys(unsigned preset);

// A key that an object may carry: a member of PrintObjectConfig or PrintRegionConfig. Every
// other preset key is print-level and its definition carries a ScopeReason.
bool is_object_level_key(const std::string &key);

// Applies every rule a row of the preset names to the config and returns one state per row
// (per extruder for the per-extruder rules). Rows without a rule are absent: always enabled.
std::vector<ToggleState> apply_toggle_rules(unsigned preset, const DynamicPrintConfig &config, size_t extruders_count);

// Every key of the FFF static configs is in exactly one row or one no-ui entry (so in one of
// the generated preset lists) unless it is a named preset-free key, every row names a
// defined key, a page with a group, a builder when it is Custom, a rule of the rule table
// when it names one, every rule is named by a row, every print-level key's definition carries
// a scope reason and no object-level key's does, every object-level key has a row in the
// object invalidation table and every other engine key one in the print table. Writes one
// line per violation and returns their count (the console's --check-settings-spec exit status).
int check_settings_spec(std::ostream &report);

} // namespace Luminary
