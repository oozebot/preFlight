///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2019 - 2021 Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

/*	 Class for validation config options
 *	 and update (enable/disable) IU components
 *	 
 *	 Used for config validation for global config (Print Settings Tab)
 *	 and local config (overrides options on sidebar)
 * */

#include "luminary/config/catalog/PrintConfig.hpp"
#include "Field.hpp"

namespace Luminary
{
class ModelConfig;
} // namespace Luminary

namespace DSKY
{
using namespace Luminary;

class ConfigManipulation
{
    bool is_msg_dlg_already_exist{false};

    bool m_is_initialized_support_material_overhangs_queried{false};
    bool m_support_material_overhangs_queried{false};

    // function to loading of changed configuration
    std::function<void()> load_config = nullptr;
    // The reason is the rule's text when the field is disabled, empty when it is enabled.
    std::function<void(const std::string &, bool toggle, int opt_index, const wxString &reason)> cb_toggle_field =
        nullptr;
    // callback to propagation of changed value, if needed
    std::function<void(const std::string &, const boost::any &)> cb_value_change = nullptr;
    ModelConfig *local_config = nullptr;
    wxWindow *m_msg_dlg_parent{nullptr};

    void toggle_rule_states(unsigned preset, DynamicPrintConfig *config, size_t extruders_count);

public:
    ConfigManipulation(
        std::function<void()> load_config,
        std::function<void(const std::string &, bool toggle, int opt_index, const wxString &reason)> cb_toggle_field,
        std::function<void(const std::string &, const boost::any &)> cb_value_change,
        ModelConfig *local_config = nullptr, wxWindow *msg_dlg_parent = nullptr)
        : load_config(load_config)
        , cb_toggle_field(cb_toggle_field)
        , cb_value_change(cb_value_change)
        , m_msg_dlg_parent(msg_dlg_parent)
        , local_config(local_config)
    {
    }
    ConfigManipulation() {}

    ~ConfigManipulation()
    {
        load_config = nullptr;
        cb_toggle_field = nullptr;
        cb_value_change = nullptr;
    }

    void apply(DynamicPrintConfig *config, DynamicPrintConfig *new_config);
    void toggle_field(const std::string &field_key, const bool toggle, int opt_index = -1,
                      const wxString &reason = wxString());

    // FFF print
    // changed_opt_key: If provided, only validates extrusion widths when the changed key is relevant
    // (an extrusion width key or nozzle_diameter). Empty string means validate all.
    void update_print_fff_config(DynamicPrintConfig *config, const bool is_global_config = false,
                                 const std::string &changed_opt_key = "");
    // The enable rules of the row specification applied to the preset's config: every row that
    // names a rule is toggled with the rule's reason. The printer rules take the extruder count
    // for the per-extruder rows.
    void toggle_print_fff_options(DynamicPrintConfig *config);
    void toggle_filament_options(DynamicPrintConfig *config);
    void toggle_printer_options(DynamicPrintConfig *config, size_t extruders_count);

    bool is_initialized_support_material_overhangs_queried()
    {
        return m_is_initialized_support_material_overhangs_queried;
    }
    void initialize_support_material_overhangs_queried(bool queried)
    {
        m_is_initialized_support_material_overhangs_queried = true;
        m_support_material_overhangs_queried = queried;
    }

    static void approve_extrusion_width(const std::string &width_key, double width_mm);
    static void clear_approved_widths();

    // Suppress all config validation dialogs during startup/GUI recreation
    // to prevent modal dialogs from deadlocking behind the splash screen
    static void set_suppress_startup_dialogs(bool suppress);
};

} // namespace DSKY

