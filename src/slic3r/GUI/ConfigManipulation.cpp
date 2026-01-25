///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2019 - 2023 Lukáš Hejl @hejllukas, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Oleksandra Iushchenko @YuSanka, Pavel Mikuš @Godrak, Tomáš Mészáros @tamasmeszaros
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
// #include "libslic3r/GCodeSender.hpp"
#include "ConfigManipulation.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "format.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "MsgDialog.hpp"

#include <cmath>
#include <map>
#include <string>
#include <wx/msgdlg.h>

namespace Slic3r
{
namespace GUI
{

// These track widths that have been explicitly approved by the user to suppress validation warnings
static std::map<std::string, double> s_approved_narrow_widths; // below 60% of nozzle
static std::map<std::string, double> s_approved_wide_widths;   // above 150% of nozzle

// Flag to suppress extrusion width warnings during initial app load
static bool s_suppress_extrusion_width_warnings = false;

void ConfigManipulation::set_suppress_extrusion_width_warnings(bool suppress)
{
    s_suppress_extrusion_width_warnings = suppress;
}

void ConfigManipulation::approve_extrusion_width(const std::string &width_key, double width_mm)
{
    // Pre-approve both narrow and wide for this key at this value
    s_approved_narrow_widths[width_key] = width_mm;
    s_approved_wide_widths[width_key] = width_mm;
}

void ConfigManipulation::clear_approved_widths()
{
    s_approved_narrow_widths.clear();
    s_approved_wide_widths.clear();
}

void ConfigManipulation::apply(DynamicPrintConfig *config, DynamicPrintConfig *new_config)
{
    bool modified = false;
    for (auto opt_key : config->diff(*new_config))
    {
        config->set_key_value(opt_key, new_config->option(opt_key)->clone());
        modified = true;
    }

    if (modified && load_config != nullptr)
        load_config();
}

void ConfigManipulation::toggle_field(const std::string &opt_key, const bool toggle, int opt_index /* = -1*/)
{
    if (local_config)
    {
        if (local_config->option(opt_key) == nullptr)
            return;
    }
    cb_toggle_field(opt_key, toggle, opt_index);
}

std::optional<DynamicPrintConfig> handle_automatic_extrusion_widths(const DynamicPrintConfig &config,
                                                                    const bool is_global_config,
                                                                    wxWindow *msg_dlg_parent)
{
    const std::vector<std::string> extrusion_width_parameters = {
        "extrusion_width",        "external_perimeter_extrusion_width", "first_layer_extrusion_width",
        "infill_extrusion_width", "perimeter_extrusion_width",          "solid_infill_extrusion_width",
        "bridge_extrusion_width", "support_material_extrusion_width",   "top_infill_extrusion_width"};

    auto is_zero_width = [](const ConfigOptionFloatOrPercent &opt) -> bool
    {
        return opt.value == 0. && !opt.percent;
    };

    auto is_parameters_adjustment_needed = [&is_zero_width, &config, &extrusion_width_parameters]() -> bool
    {
        if (!config.opt_bool("automatic_extrusion_widths"))
        {
            return false;
        }

        for (const std::string &extrusion_width_parameter : extrusion_width_parameters)
        {
            if (!is_zero_width(*config.option<ConfigOptionFloatOrPercent>(extrusion_width_parameter)))
            {
                return true;
            }
        }

        return false;
    };

    if (is_parameters_adjustment_needed())
    {
        wxString msg_text = _(L("The automatic extrusion widths calculation requires:\n"
                                "- Default extrusion width: 0\n"
                                "- First layer extrusion width: 0\n"
                                "- Perimeter extrusion width: 0\n"
                                "- External perimeter extrusion width: 0\n"
                                "- Infill extrusion width: 0\n"
                                "- Solid infill extrusion width: 0\n"
                                "- Top infill extrusion width: 0\n"
                                "- Support material extrusion width: 0"));

        if (is_global_config)
        {
            msg_text += "\n\n" +
                        _(L("Shall I adjust those settings in order to enable automatic extrusion widths calculation?"));
        }

        MessageDialog dialog(msg_dlg_parent, msg_text, _(L("Automatic extrusion widths calculation")),
                             wxICON_WARNING | (is_global_config ? wxYES | wxNO : wxOK));

        const int answer = dialog.ShowModal();
        DynamicPrintConfig new_conf = config;
        if (!is_global_config || answer == wxID_YES)
        {
            for (const std::string &extrusion_width_parameter : extrusion_width_parameters)
            {
                new_conf.set_key_value(extrusion_width_parameter, new ConfigOptionFloatOrPercent(0., false));
            }
        }
        else
        {
            new_conf.set_key_value("automatic_extrusion_widths", new ConfigOptionBool(false));
        }

        return new_conf;
    }

    return std::nullopt;
}

void ConfigManipulation::update_print_fff_config(DynamicPrintConfig *config, const bool is_global_config)
{
    // #ys_FIXME_to_delete
    //! Temporary workaround for the correct updates of the TextCtrl (like "layer_height"):
    // KillFocus() for the wxSpinCtrl use CallAfter function. So,
    // to except the duplicate call of the update() after dialog->ShowModal(),
    // let check if this process is already started.
    if (is_msg_dlg_already_exist)
        return;

    // layer_height shouldn't be equal to zero
    if (config->opt_float("layer_height") < EPSILON)
    {
        const wxString msg_text = _(L("Layer height is not valid.\n\nThe layer height will be reset to 0.01."));
        MessageDialog dialog(m_msg_dlg_parent, msg_text, _(L("Layer height")), wxICON_WARNING | wxOK);
        DynamicPrintConfig new_conf = *config;
        is_msg_dlg_already_exist = true;
        dialog.ShowModal();
        new_conf.set_key_value("layer_height", new ConfigOptionFloat(0.01));
        apply(config, &new_conf);
        is_msg_dlg_already_exist = false;
    }

    if (config->option<ConfigOptionFloatOrPercent>("first_layer_height")->value < EPSILON)
    {
        const wxString msg_text = _(
            L("First layer height is not valid.\n\nThe first layer height will be reset to 0.01."));
        MessageDialog dialog(m_msg_dlg_parent, msg_text, _(L("First layer height")), wxICON_WARNING | wxOK);
        DynamicPrintConfig new_conf = *config;
        is_msg_dlg_already_exist = true;
        dialog.ShowModal();
        new_conf.set_key_value("first_layer_height", new ConfigOptionFloatOrPercent(0.01, false));
        apply(config, &new_conf);
        is_msg_dlg_already_exist = false;
    }

    // Helper lambda to clamp overlap values and warn user (uses WarningDialog style)
    auto clamp_overlap = [&](const std::string &opt_key, const std::string &ref_width_key, double min_percent,
                             double max_percent, const std::string &label, const std::string &ref_width_label)
    {
        auto *overlap_opt = config->option<ConfigOptionFloatOrPercent>(opt_key);
        if (!overlap_opt)
            return;

        // Get reference width for mm clamping
        // Note: nozzle_diameter is in printer config, not print config
        const DynamicPrintConfig &printer_config = wxGetApp().preset_bundle->printers.get_selected_preset().config;
        auto *nozzle_opt = printer_config.option<ConfigOptionFloats>("nozzle_diameter");
        double nozzle_diam = (nozzle_opt && !nozzle_opt->values.empty()) ? nozzle_opt->values[0] : 0.4;

        double ref_width = 0.0;
        auto *width_opt = config->option<ConfigOptionFloatOrPercent>(ref_width_key);
        if (width_opt)
        {
            if (width_opt->percent)
            {
                // Width is percentage of nozzle - resolve it
                ref_width = nozzle_diam * width_opt->value / 100.0;
            }
            else if (width_opt->value > 0)
            {
                ref_width = width_opt->value;
            }
        }
        // If width is 0 (auto) or very small, use nozzle diameter as reference
        if (ref_width < 0.1)
        {
            ref_width = nozzle_diam;
        }

        double min_mm = ref_width * min_percent / 100.0;
        double max_mm = ref_width * max_percent / 100.0;

        bool needs_clamp = false;
        bool exceeded_max = false;
        double new_value = overlap_opt->value;
        bool new_percent = overlap_opt->percent;

        if (overlap_opt->percent)
        {
            // Percentage mode
            if (overlap_opt->value > max_percent)
            {
                new_value = max_percent;
                needs_clamp = true;
                exceeded_max = true;
            }
            else if (overlap_opt->value < min_percent)
            {
                new_value = min_percent;
                needs_clamp = true;
                exceeded_max = false;
            }
        }
        else
        {
            // Absolute mm mode
            if (overlap_opt->value > max_mm + 0.001)
            {
                new_value = max_mm;
                needs_clamp = true;
                exceeded_max = true;
            }
            else if (overlap_opt->value < min_mm - 0.001)
            {
                new_value = min_mm;
                needs_clamp = true;
                exceeded_max = false;
            }
        }

        if (needs_clamp)
        {
            // Build descriptive message
            wxString limit_desc;
            if (exceeded_max)
            {
                if (max_percent == 100.0)
                    limit_desc = wxString::Format(_L("cannot be greater than %s"), ref_width_label);
                else
                    limit_desc = wxString::Format(_L("cannot be greater than %d%% of %s"), (int) max_percent,
                                                  ref_width_label);
            }
            else
            {
                if (min_percent == -100.0)
                    limit_desc = wxString::Format(_L("cannot be less than -%s (negative %s)"), ref_width_label,
                                                  ref_width_label);
                else
                    limit_desc = wxString::Format(_L("cannot be less than %d%% of %s"), (int) min_percent,
                                                  ref_width_label);
            }

            wxString new_value_str;
            if (new_percent)
                new_value_str = wxString::Format("%.2f%%", new_value);
            else
                new_value_str = wxString::Format("%.3f mm", new_value);

            wxString msg_text = wxString::Format(_L("%s %s.\n\nThe value has been set to %s."), label, limit_desc,
                                                 new_value_str);

            WarningDialog dialog(m_msg_dlg_parent, msg_text, _L("Parameter validation") + ": " + opt_key, wxOK);
            DynamicPrintConfig new_conf = *config;
            new_conf.set_key_value(opt_key, new ConfigOptionFloatOrPercent(new_value, new_percent));
            is_msg_dlg_already_exist = true;
            dialog.ShowModal();
            apply(config, &new_conf);
            is_msg_dlg_already_exist = false;
        }
    };

    // Clamp external perimeter overlap: -100% to 100%
    clamp_overlap("external_perimeter_overlap", "perimeter_extrusion_width", -100.0, 100.0,
                  _L("External perimeter/perimeter overlap").ToStdString(),
                  _L("Perimeter extrusion width").ToStdString());

    // Clamp perimeter/perimeter overlap: -100% to 80%
    clamp_overlap("perimeter_perimeter_overlap", "perimeter_extrusion_width", -100.0, 80.0,
                  _L("Perimeter/perimeter overlap").ToStdString(), _L("Perimeter extrusion width").ToStdString());

    // Clamp infill/perimeters overlap: -100% to 100%
    clamp_overlap("infill_overlap", "perimeter_extrusion_width", -100.0, 100.0,
                  _L("Infill/perimeters overlap").ToStdString(), _L("Perimeter extrusion width").ToStdString());

    // Clamp bridge infill/perimeters overlap: -100% to 100%
    clamp_overlap("bridge_infill_perimeter_overlap", "perimeter_extrusion_width", -100.0, 100.0,
                  _L("Bridge infill/perimeters overlap").ToStdString(), _L("Perimeter extrusion width").ToStdString());

    // Clamp bridge infill overlap: -100% to 80%
    clamp_overlap("bridge_infill_overlap", "bridge_extrusion_width", -100.0, 80.0,
                  _L("Bridge infill overlap").ToStdString(), _L("Bridge extrusion width").ToStdString());

    // Note: s_approved_narrow_widths and s_approved_wide_widths are now file-scope statics (s_approved_*)
    // to allow pre-approval via ConfigManipulation::approve_extrusion_width()

    // Helper lambda to validate extrusion width against its corresponding nozzle
    auto validate_extrusion_width =
        [&](const std::string &width_key, const std::string &extruder_key, const std::string &label)
    {
        if (is_msg_dlg_already_exist)
            return;

        // Skip validation during initial app load (user hasn't configured anything yet)
        if (s_suppress_extrusion_width_warnings)
            return;

        auto *width_opt = config->option<ConfigOptionFloatOrPercent>(width_key);
        if (!width_opt)
            return;

        // Get the extruder index (1-based in config, convert to 0-based for nozzle array)
        int extruder_idx = 0;
        if (!extruder_key.empty())
        {
            auto *extruder_opt = config->option<ConfigOptionInt>(extruder_key);
            if (extruder_opt && extruder_opt->value > 0)
                extruder_idx = extruder_opt->value - 1;
        }

        // Get nozzle diameter from printer config (not print config)
        // Must use get_edited_preset() not get_selected_preset() because changes are in
        // the edited preset until saved. Selected preset has the old/saved values.
        const DynamicPrintConfig &printer_config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
        auto *nozzle_opt = printer_config.option<ConfigOptionFloats>("nozzle_diameter");
        if (!nozzle_opt || nozzle_opt->values.empty())
            return;

        double nozzle_diam = nozzle_opt->values[std::min(extruder_idx, (int) nozzle_opt->values.size() - 1)];
        if (nozzle_diam < 0.1)
            return; // Invalid nozzle

        // Calculate the actual width in mm
        double width_mm = 0.0;
        if (width_opt->percent)
        {
            width_mm = nozzle_diam * width_opt->value / 100.0;
        }
        else
        {
            width_mm = width_opt->value;
        }

        // If width is effectively 0 (< 0.001), normalize to 0 (auto)
        if (width_mm < 0.001)
        {
            if (width_opt->value != 0.0)
            {
                // Update UI to show 0
                DynamicPrintConfig new_conf = *config;
                new_conf.set_key_value(width_key, new ConfigOptionFloatOrPercent(0.0, false));
                apply(config, &new_conf);
            }
            return;
        }

        double min_width = nozzle_diam * 0.6; // 60% of nozzle
        double max_width = nozzle_diam * 1.5; // 150% of nozzle

        // Check if width is within valid range (60% - 150%)
        bool is_too_narrow = width_mm < min_width - 0.001;
        bool is_too_wide = width_mm > max_width + 0.001;

        if (!is_too_narrow && !is_too_wide)
        {
            // Width is valid - remove from approved lists if it was there
            s_approved_narrow_widths.erase(width_key);
            s_approved_wide_widths.erase(width_key);
            return;
        }

        // Check if this exact value was already approved
        if (is_too_narrow)
        {
            auto it = s_approved_narrow_widths.find(width_key);
            if (it != s_approved_narrow_widths.end() && std::abs(it->second - width_mm) < 0.0001)
            {
                return; // Already approved
            }
        }
        if (is_too_wide)
        {
            auto it = s_approved_wide_widths.find(width_key);
            if (it != s_approved_wide_widths.end() && std::abs(it->second - width_mm) < 0.0001)
            {
                return; // Already approved
            }
        }

        // Build warning message
        wxString width_str;
        if (width_opt->percent)
            width_str = wxString::Format("%.0f%%", width_opt->value);
        else
            width_str = wxString::Format("%.3f mm", width_opt->value);

        wxString msg_text;
        if (is_too_narrow)
        {
            msg_text = wxString::Format(_L("%s is set to %s, which is below 60%% of the nozzle diameter (%.2f mm).\n\n"
                                           "Extrusion widths below 60%% of nozzle size may cause printing issues.\n\n"
                                           "Do you want to keep this value?\n"
                                           "Select YES to keep %s,\n"
                                           "or NO to reset to %.2f mm (nozzle diameter)."),
                                        label, width_str, nozzle_diam, width_str, nozzle_diam);
        }
        else
        {
            msg_text = wxString::Format(_L("%s is set to %s, which exceeds 150%% of the nozzle diameter (%.2f mm).\n\n"
                                           "Extrusion widths above 150%% of nozzle size may cause printing issues.\n\n"
                                           "Do you want to keep this value?\n"
                                           "Select YES to keep %s,\n"
                                           "or NO to reset to %.2f mm (nozzle diameter)."),
                                        label, width_str, nozzle_diam, width_str, nozzle_diam);
        }

        WarningDialog dialog(m_msg_dlg_parent, msg_text, _L("Parameter validation") + ": " + width_key, wxYES | wxNO);
        is_msg_dlg_already_exist = true;
        if (dialog.ShowModal() == wxID_YES)
        {
            // User approved this out-of-range width - remember it
            if (is_too_narrow)
                s_approved_narrow_widths[width_key] = width_mm;
            else
                s_approved_wide_widths[width_key] = width_mm;
        }
        else
        {
            // User rejected - reset to nozzle diameter
            s_approved_narrow_widths.erase(width_key);
            s_approved_wide_widths.erase(width_key);
            DynamicPrintConfig new_conf = *config;
            new_conf.set_key_value(width_key, new ConfigOptionFloatOrPercent(nozzle_diam, false));
            apply(config, &new_conf);
        }
        is_msg_dlg_already_exist = false;
    };

    // Validate all extrusion widths against their corresponding nozzles
    validate_extrusion_width("extrusion_width", "", _L("Default extrusion width").ToStdString());
    validate_extrusion_width("first_layer_extrusion_width", "", _L("First layer extrusion width").ToStdString());
    validate_extrusion_width("perimeter_extrusion_width", "perimeter_extruder",
                             _L("Perimeter extrusion width").ToStdString());
    validate_extrusion_width("external_perimeter_extrusion_width", "perimeter_extruder",
                             _L("External perimeter extrusion width").ToStdString());
    validate_extrusion_width("infill_extrusion_width", "infill_extruder", _L("Infill extrusion width").ToStdString());
    validate_extrusion_width("solid_infill_extrusion_width", "solid_infill_extruder",
                             _L("Solid infill extrusion width").ToStdString());
    validate_extrusion_width("top_infill_extrusion_width", "solid_infill_extruder",
                             _L("Top infill extrusion width").ToStdString());
    validate_extrusion_width("support_material_extrusion_width", "support_material_extruder",
                             _L("Support material extrusion width").ToStdString());
    validate_extrusion_width("support_material_interface_extrusion_width", "support_material_interface_extruder",
                             _L("Support material interface extrusion width").ToStdString());
    validate_extrusion_width("bridge_extrusion_width", "perimeter_extruder",
                             _L("Bridge extrusion width").ToStdString());

    double fill_density = config->option<ConfigOptionPercent>("fill_density")->value;

    if (config->opt_bool("spiral_vase") &&
        !(config->opt_int("perimeters") == 1 && config->opt_int("top_solid_layers") == 0 && fill_density == 0 &&
          !config->opt_bool("support_material") && config->opt_int("support_material_enforce_layers") == 0 &&
          !config->opt_bool("thin_walls")))
    {
        wxString msg_text = _(L("The Spiral Vase mode requires:\n"
                                "- one perimeter\n"
                                "- no top solid layers\n"
                                "- 0% fill density\n"
                                "- no support material\n"
                                "- Detect thin walls disabled"));
        if (is_global_config)
            msg_text += "\n\n" + _(L("Shall I adjust those settings in order to enable Spiral Vase?"));
        MessageDialog dialog(m_msg_dlg_parent, msg_text, _(L("Spiral Vase")),
                             wxICON_WARNING | (is_global_config ? wxYES | wxNO : wxOK));
        DynamicPrintConfig new_conf = *config;
        auto answer = dialog.ShowModal();
        bool support = true;
        if (!is_global_config || answer == wxID_YES)
        {
            new_conf.set_key_value("perimeters", new ConfigOptionInt(1));
            new_conf.set_key_value("top_solid_layers", new ConfigOptionInt(0));
            new_conf.set_key_value("fill_density", new ConfigOptionPercent(0));
            new_conf.set_key_value("support_material", new ConfigOptionBool(false));
            new_conf.set_key_value("support_material_enforce_layers", new ConfigOptionInt(0));
            new_conf.set_key_value("thin_walls", new ConfigOptionBool(false));
            fill_density = 0;
            support = false;
        }
        else
        {
            new_conf.set_key_value("spiral_vase", new ConfigOptionBool(false));
        }
        apply(config, &new_conf);
        if (cb_value_change)
        {
            cb_value_change("fill_density", fill_density);
            if (!support)
                cb_value_change("support_material", false);
        }
    }

    if (config->opt_bool("wipe_tower") && config->opt_bool("support_material") &&
        // Organic supports are always synchronized with object layers as of now.
        config->opt_enum<SupportMaterialStyle>("support_material_style") != smsOrganic)
    {
        if (config->opt_enum<SupportTopContactGap>("support_material_contact_distance") == stcgNoGap)
        {
            if (!config->opt_bool("support_material_synchronize_layers"))
            {
                wxString msg_text = _(L("For the Wipe Tower to work with the soluble supports, the support layers\n"
                                        "need to be synchronized with the object layers."));
                if (is_global_config)
                    msg_text += "\n\n" + _(L("Shall I synchronize support layers in order to enable the Wipe Tower?"));
                MessageDialog dialog(m_msg_dlg_parent, msg_text, _(L("Wipe Tower")),
                                     wxICON_WARNING | (is_global_config ? wxYES | wxNO : wxOK));
                DynamicPrintConfig new_conf = *config;
                auto answer = dialog.ShowModal();
                if (!is_global_config || answer == wxID_YES)
                {
                    new_conf.set_key_value("support_material_synchronize_layers", new ConfigOptionBool(true));
                }
                else
                    new_conf.set_key_value("wipe_tower", new ConfigOptionBool(false));
                apply(config, &new_conf);
            }
        }
        else
        {
            if ((config->opt_int("support_material_extruder") != 0 ||
                 config->opt_int("support_material_interface_extruder") != 0))
            {
                wxString msg_text = _(
                    L("The Wipe Tower currently supports the non-soluble supports only "
                      "if they are printed with the current extruder without triggering a tool change. "
                      "(both support_material_extruder and support_material_interface_extruder need to be set to 0)."));
                if (is_global_config)
                    msg_text += "\n\n" + _(L("Shall I adjust those settings in order to enable the Wipe Tower?"));
                MessageDialog dialog(m_msg_dlg_parent, msg_text, _(L("Wipe Tower")),
                                     wxICON_WARNING | (is_global_config ? wxYES | wxNO : wxOK));
                DynamicPrintConfig new_conf = *config;
                auto answer = dialog.ShowModal();
                if (!is_global_config || answer == wxID_YES)
                {
                    new_conf.set_key_value("support_material_extruder", new ConfigOptionInt(0));
                    new_conf.set_key_value("support_material_interface_extruder", new ConfigOptionInt(0));
                }
                else
                    new_conf.set_key_value("wipe_tower", new ConfigOptionBool(false));
                apply(config, &new_conf);
            }
        }
    }

    // Check "support_material" and "overhangs" relations only on global settings level
    if (is_global_config && config->opt_bool("support_material"))
    {
        // Ask only once.
        if (!m_support_material_overhangs_queried)
        {
            m_support_material_overhangs_queried = true;
            if (!config->opt_bool("overhangs") /* != 1*/)
            {
                wxString msg_text = _(L("Supports work better, if the following feature is enabled:\n"
                                        "- Detect bridging perimeters"));
                if (is_global_config)
                    msg_text += "\n\n" + _(L("Shall I adjust those settings for supports?"));
                MessageDialog dialog(m_msg_dlg_parent, msg_text, _L("Support Generator"),
                                     wxICON_WARNING | wxYES | wxNO);
                DynamicPrintConfig new_conf = *config;
                auto answer = dialog.ShowModal();
                if (answer == wxID_YES)
                {
                    // Enable "detect bridging perimeters".
                    new_conf.set_key_value("overhangs", new ConfigOptionBool(true));
                }
                //else Do nothing, leave supports on and "detect bridging perimeters" off.
                apply(config, &new_conf);
            }
        }
    }
    else
    {
        m_support_material_overhangs_queried = false;
    }

    if (config->option<ConfigOptionPercent>("fill_density")->value == 100)
    {
        const int fill_pattern = config->option<ConfigOptionEnum<InfillPattern>>("fill_pattern")->value;
        if (bool correct_100p_fill =
                config->option_def("top_fill_pattern")->enum_def->enum_to_index(fill_pattern).has_value();
            !correct_100p_fill)
        {
            // get fill_pattern name from enum_labels for using this one at dialog_msg
            const ConfigOptionDef *fill_pattern_def = config->option_def("fill_pattern");
            assert(fill_pattern_def != nullptr);
            if (auto label = fill_pattern_def->enum_def->enum_to_label(fill_pattern); label.has_value())
            {
                wxString msg_text = GUI::format_wxstr(
                    _L("The %1% infill pattern is not supposed to work at 100%% density."), _(*label));
                if (is_global_config)
                    msg_text += "\n\n" + _L("Shall I switch to rectilinear fill pattern?");
                MessageDialog dialog(m_msg_dlg_parent, msg_text, _L("Infill"),
                                     wxICON_WARNING | (is_global_config ? wxYES | wxNO : wxOK));
                DynamicPrintConfig new_conf = *config;
                auto answer = dialog.ShowModal();
                if (!is_global_config || answer == wxID_YES)
                {
                    new_conf.set_key_value("fill_pattern", new ConfigOptionEnum<InfillPattern>(ipRectilinear));
                    fill_density = 100;
                }
                else
                    fill_density = wxGetApp()
                                       .preset_bundle->prints.get_selected_preset()
                                       .config.option<ConfigOptionPercent>("fill_density")
                                       ->value;
                new_conf.set_key_value("fill_density", new ConfigOptionPercent(fill_density));
                apply(config, &new_conf);
                if (cb_value_change)
                    cb_value_change("fill_density", fill_density);
            }
        }
    }

    if (config->opt_bool("automatic_extrusion_widths"))
    {
        std::optional<DynamicPrintConfig> new_config = handle_automatic_extrusion_widths(*config, is_global_config,
                                                                                         m_msg_dlg_parent);
        if (new_config.has_value())
        {
            apply(config, &(*new_config));
        }
    }
}

void ConfigManipulation::toggle_print_fff_options(DynamicPrintConfig *config)
{
    bool have_perimeters = config->opt_int("perimeters") > 0;
    for (auto el :
         {"extra_perimeters", "extra_perimeters_on_overhangs", "thin_walls", "overhangs", "seam_position",
          "staggered_inner_seams", "external_perimeters_first", "external_perimeter_extrusion_width", "perimeter_speed",
          "small_perimeter_speed", "external_perimeter_speed", "enable_dynamic_overhang_speeds"})
        toggle_field(el, have_perimeters);

    for (size_t i = 0; i < 4; i++)
    {
        toggle_field("overhang_speed_" + std::to_string(i), config->opt_bool("enable_dynamic_overhang_speeds"));
    }

    const bool have_infill = config->option<ConfigOptionPercent>("fill_density")->value > 0;
    const bool has_automatic_infill_combination =
        config->option<ConfigOptionBool>("automatic_infill_combination")->value;
    // infill_extruder uses the same logic as in Print::extruders()
    for (auto el : {"fill_pattern", "solid_infill_every_layers", "solid_infill_below_area", "infill_extruder",
                    "infill_anchor_max", "automatic_infill_combination"})
    {
        toggle_field(el, have_infill);
    }

    toggle_field("infill_every_layers", have_infill && !has_automatic_infill_combination);
    toggle_field("automatic_infill_combination_max_layer_height", have_infill && has_automatic_infill_combination);

    // Only allow configuration of open anchors if the anchoring is enabled.
    bool has_infill_anchors = have_infill && config->option<ConfigOptionFloatOrPercent>("infill_anchor_max")->value > 0;
    toggle_field("infill_anchor", has_infill_anchors);

    bool has_spiral_vase = config->opt_bool("spiral_vase");
    bool has_top_solid_infill = config->opt_int("top_solid_layers") > 0;
    bool has_bottom_solid_infill = config->opt_int("bottom_solid_layers") > 0;
    bool has_solid_infill = has_top_solid_infill || has_bottom_solid_infill;
    // solid_infill_extruder uses the same logic as in Print::extruders()
    for (auto el : {"top_fill_pattern", "bottom_fill_pattern", "infill_first", "solid_infill_extruder",
                    "solid_infill_extrusion_width", "solid_infill_speed"})
        toggle_field(el, has_solid_infill);

    for (auto el :
         {"fill_angle", "bridge_angle", "infill_extrusion_width", "infill_speed", "bridge_speed", "over_bridge_speed"})
        toggle_field(el, have_infill || has_solid_infill);

    bool has_narrow_solid_concentric = config->opt_bool("narrow_solid_infill_concentric");
    toggle_field("narrow_solid_infill_threshold", has_narrow_solid_concentric);

    const bool has_ensure_vertical_shell_thickness = config->opt_enum<EnsureVerticalShellThickness>(
                                                         "ensure_vertical_shell_thickness") !=
                                                     EnsureVerticalShellThickness::Disabled;
    toggle_field("top_solid_min_thickness",
                 !has_spiral_vase && has_top_solid_infill && has_ensure_vertical_shell_thickness);
    toggle_field("bottom_solid_min_thickness",
                 !has_spiral_vase && has_bottom_solid_infill && has_ensure_vertical_shell_thickness);

    // Gap fill is newly allowed in between perimeter lines even for empty infill (see GH #1476).
    toggle_field("gap_fill_speed", have_perimeters);

    // Note: Base fuzzy skin options (thickness, point_dist, etc.) are ALWAYS shown because:
    // 1. They apply to global fuzzy skin (when type != None)
    // 2. They ALSO apply to paint-on fuzzy skin (which works when type == None)
    // So users can configure noise parameters for painted areas even with global type = None

    FuzzySkinNoiseType noise_type = config->opt_enum<FuzzySkinNoiseType>("fuzzy_skin_noise_type");
    bool have_structured_noise = noise_type != FuzzySkinNoiseType::Classic;
    toggle_field("fuzzy_skin_scale", have_structured_noise);

    // Octaves only apply to Perlin, Billow, and Ridged noise
    bool have_octaves = have_structured_noise && noise_type != FuzzySkinNoiseType::Voronoi;
    toggle_field("fuzzy_skin_octaves", have_octaves);

    // Persistence only applies to Perlin and Billow
    bool have_persistence = have_structured_noise &&
                            (noise_type == FuzzySkinNoiseType::Perlin || noise_type == FuzzySkinNoiseType::Billow);
    toggle_field("fuzzy_skin_persistence", have_persistence);

    // fuzzy_skin_point_placement applies to all fuzzy skin modes, so always visible

    bool interlock_enabled = config->opt_bool("interlock_perimeters_enabled");
    toggle_field("interlock_perimeter_count", interlock_enabled);
    // interlock_perimeter_strength hidden - forced to 100% in code
    toggle_field("interlock_perimeter_overlap", interlock_enabled);
    toggle_field("interlock_flow_detection", interlock_enabled);

    bool has_top_surface_flow_reduction = config->option<ConfigOptionPercent>("top_surface_flow_reduction")->value > 0;
    toggle_field("top_surface_visibility_detection", has_top_surface_flow_reduction);

    for (auto el : {"top_infill_extrusion_width", "top_solid_infill_speed"})
        toggle_field(el, has_top_solid_infill || (has_spiral_vase && has_bottom_solid_infill));

    bool have_default_acceleration = config->opt_float("default_acceleration") > 0;
    for (auto el : {"perimeter_acceleration", "infill_acceleration", "top_solid_infill_acceleration",
                    "solid_infill_acceleration", "external_perimeter_acceleration", "bridge_acceleration",
                    "first_layer_acceleration", "wipe_tower_acceleration"})
        toggle_field(el, have_default_acceleration);

    bool have_skirt = config->opt_int("skirts") > 0;
    toggle_field("skirt_height", have_skirt && config->opt_enum<DraftShield>("draft_shield") != dsEnabled);
    for (auto el : {"skirt_distance", "draft_shield", "min_skirt_length"})
        toggle_field(el, have_skirt);

    bool have_brim = config->opt_enum<BrimType>("brim_type") != btNoBrim;
    for (auto el : {"brim_width", "brim_separation", "brim_ears_max_angle", "brim_ears_detection_length"})
        toggle_field(el, have_brim);
    // perimeter_extruder uses the same logic as in Print::extruders()
    toggle_field("perimeter_extruder", have_perimeters || have_brim);

    bool have_raft = config->opt_int("raft_layers") > 0;
    bool have_support_material = config->opt_bool("support_material") || have_raft;
    bool have_support_material_auto = have_support_material && config->opt_bool("support_material_auto");
    bool have_support_interface = config->opt_int("support_material_interface_layers") > 0;
    bool have_support_soluble = have_support_material && config->opt_enum<SupportTopContactGap>(
                                                             "support_material_contact_distance") == stcgNoGap;
    auto support_material_style = config->opt_enum<SupportMaterialStyle>("support_material_style");
    // Note: support_material_extrusion_width is NOT toggled here - it should always be visible
    // like other extrusion width settings, so users can configure it before enabling supports.
    for (auto el : {"support_material_pattern", "support_material_with_sheath", "support_material_spacing",
                    "support_material_angle", "support_material_interface_pattern", "support_material_interface_layers",
                    "dont_support_bridges", "support_material_contact_distance", "support_material_xy_spacing"})
        toggle_field(el, have_support_material);
    toggle_field("support_material_style", have_support_material_auto);
    toggle_field("support_material_threshold", have_support_material_auto);
    // Original logic disabled bottom contact distance when top was set to "NoGap" (assuming soluble supports).
    // But user may want no top gap with a bottom gap - these should be independent settings.
    toggle_field("support_material_bottom_contact_distance", have_support_material);
    bool have_custom_top_gap = have_support_material && !have_support_soluble &&
                               config->opt_enum<SupportTopContactGap>("support_material_contact_distance") ==
                                   stcgCustom;
    toggle_field("support_material_contact_distance_custom", have_custom_top_gap);
    // Removed ! have_support_soluble check - bottom settings should be independent of top gap
    bool have_half_layer_gap = have_support_material &&
                               config->opt_enum<SupportBottomContactGap>("support_material_bottom_contact_distance") ==
                                   sbcgHalfLayer;
    toggle_field("support_material_bottom_contact_extrusion_width", have_half_layer_gap);
    // Closing radius is used by Snug and Organic supports to close small holes in interface layers.
    // Since paint-on supports can specify any type regardless of the Style dropdown,
    // this setting should always be available when support is enabled.
    toggle_field("support_material_closing_radius", have_support_material);
    toggle_field("support_material_min_area", have_support_material);

    // Paint-on supports can use Organic regardless of the Style dropdown setting,
    // so organic options should be available whenever support is enabled.
    const bool has_organic_supports = config->opt_bool("support_material") ||
                                      config->opt_int("support_material_enforce_layers") > 0;
    for (const std::string &key :
         {"support_tree_angle", "support_tree_angle_slow", "support_tree_branch_diameter",
          "support_tree_branch_diameter_angle", "support_tree_branch_diameter_double_wall", "support_tree_tip_diameter",
          "support_tree_branch_distance", "support_tree_top_rate"})
        toggle_field(key, has_organic_supports);

    for (auto el : {"support_material_bottom_interface_layers", "support_material_interface_spacing",
                    "support_material_interface_extruder", "support_material_interface_speed",
                    "support_material_interface_contact_loops"})
        toggle_field(el, have_support_material && have_support_interface);
    // toggle_field("support_material_synchronize_layers", have_support_soluble);

    toggle_field("perimeter_extrusion_width", have_perimeters || have_skirt || have_brim);
    toggle_field("support_material_extruder", have_support_material || have_skirt);
    toggle_field("support_material_speed", have_support_material || have_brim || have_skirt);

    toggle_field("raft_contact_distance", have_raft && !have_support_soluble);
    for (auto el : {"raft_expansion", "first_layer_acceleration_over_raft", "first_layer_speed_over_raft"})
        toggle_field(el, have_raft);

    bool has_ironing = config->opt_bool("ironing");
    for (auto el : {"ironing_type", "ironing_flowrate", "ironing_spacing", "ironing_speed"})
        toggle_field(el, has_ironing);

    bool have_ooze_prevention = config->opt_bool("ooze_prevention");
    toggle_field("standby_temperature_delta", have_ooze_prevention);

    bool have_wipe_tower = config->opt_bool("wipe_tower");
    for (auto el : {"wipe_tower_width", "wipe_tower_brim_width", "wipe_tower_cone_angle", "wipe_tower_extra_spacing",
                    "wipe_tower_extra_flow", "wipe_tower_bridging", "wipe_tower_no_sparse_layers",
                    "single_extruder_multi_material_priming"})
        toggle_field(el, have_wipe_tower);

    toggle_field("avoid_crossing_curled_overhangs", !config->opt_bool("avoid_crossing_perimeters"));
    toggle_field("avoid_crossing_perimeters", !config->opt_bool("avoid_crossing_curled_overhangs"));

    bool have_avoid_crossing_perimeters = config->opt_bool("avoid_crossing_perimeters");
    toggle_field("avoid_crossing_perimeters_max_detour", have_avoid_crossing_perimeters);

    bool have_arachne = config->opt_enum<PerimeterGeneratorType>("perimeter_generator") ==
                        PerimeterGeneratorType::Arachne;
    bool have_athena = config->opt_enum<PerimeterGeneratorType>("perimeter_generator") ==
                       PerimeterGeneratorType::Athena;
    bool have_advanced_perimeters = have_arachne || have_athena;

    toggle_field("wall_transition_length", have_advanced_perimeters);
    toggle_field("wall_transition_filter_deviation", have_advanced_perimeters);
    toggle_field("wall_transition_angle", have_advanced_perimeters);
    // Athena hardcodes this to 1 (innermost only) since it maintains fixed widths
    toggle_field("wall_distribution_count", have_arachne);
    toggle_field("min_feature_size", have_advanced_perimeters);
    // Athena uses perimeter compression based on actual perimeter widths instead
    toggle_field("min_bead_width", have_arachne);
    // toggle_field("thin_walls", !have_advanced_perimeters);

    toggle_field("perimeter_compression", have_athena);

    toggle_field("scarf_seam_placement", !has_spiral_vase);
    const auto scarf_seam_placement{config->opt_enum<ScarfSeamPlacement>("scarf_seam_placement")};
    const bool uses_scarf_seam{!has_spiral_vase && scarf_seam_placement != ScarfSeamPlacement::nowhere};
    toggle_field("scarf_seam_only_on_smooth", uses_scarf_seam);
    toggle_field("scarf_seam_start_height", uses_scarf_seam);
    toggle_field("scarf_seam_entire_loop", uses_scarf_seam);
    toggle_field("scarf_seam_length", uses_scarf_seam);
    toggle_field("scarf_seam_max_segment_length", uses_scarf_seam);
    toggle_field("scarf_seam_on_inner_perimeters", uses_scarf_seam);

    bool use_beam_interlocking = config->opt_bool("interlocking_beam");
    toggle_field("interlocking_beam_width", use_beam_interlocking);
    toggle_field("interlocking_orientation", use_beam_interlocking);
    toggle_field("interlocking_beam_layer_count", use_beam_interlocking);
    toggle_field("interlocking_depth", use_beam_interlocking);
    toggle_field("interlocking_boundary_avoidance", use_beam_interlocking);
    toggle_field("mmu_segmented_region_max_width", !use_beam_interlocking);

    bool have_non_zero_mmu_segmented_region_max_width = !use_beam_interlocking &&
                                                        config->opt_float("mmu_segmented_region_max_width") > 0.;
    toggle_field("mmu_segmented_region_interlocking_depth", have_non_zero_mmu_segmented_region_max_width);
}

void ConfigManipulation::toggle_print_sla_options(DynamicPrintConfig *config)
{
    bool supports_en = config->opt_bool("supports_enable");
    sla::SupportTreeType treetype = config->opt_enum<sla::SupportTreeType>("support_tree_type");
    bool is_default_tree = treetype == sla::SupportTreeType::Default;
    bool is_branching_tree = treetype == sla::SupportTreeType::Branching;

    toggle_field("support_tree_type", supports_en);

    toggle_field("support_head_front_diameter", supports_en && is_default_tree);
    toggle_field("support_head_penetration", supports_en && is_default_tree);
    toggle_field("support_head_width", supports_en && is_default_tree);
    toggle_field("support_pillar_diameter", supports_en && is_default_tree);
    toggle_field("support_small_pillar_diameter_percent", supports_en && is_default_tree);
    toggle_field("support_max_bridges_on_pillar", supports_en && is_default_tree);
    toggle_field("support_pillar_connection_mode", supports_en && is_default_tree);
    toggle_field("support_buildplate_only", supports_en && is_default_tree);
    toggle_field("support_base_diameter", supports_en && is_default_tree);
    toggle_field("support_base_height", supports_en && is_default_tree);
    toggle_field("support_base_safety_distance", supports_en && is_default_tree);
    toggle_field("support_critical_angle", supports_en && is_default_tree);
    toggle_field("support_max_bridge_length", supports_en && is_default_tree);
    toggle_field("support_enforcers_only", supports_en);
    toggle_field("support_max_pillar_link_distance", supports_en && is_default_tree);
    toggle_field("support_pillar_widening_factor", false);
    toggle_field("support_max_weight_on_model", false);

    toggle_field("branchingsupport_head_front_diameter", supports_en && is_branching_tree);
    toggle_field("branchingsupport_head_penetration", supports_en && is_branching_tree);
    toggle_field("branchingsupport_head_width", supports_en && is_branching_tree);
    toggle_field("branchingsupport_pillar_diameter", supports_en && is_branching_tree);
    toggle_field("branchingsupport_small_pillar_diameter_percent", supports_en && is_branching_tree);
    toggle_field("branchingsupport_max_bridges_on_pillar", false);
    toggle_field("branchingsupport_pillar_connection_mode", false);
    toggle_field("branchingsupport_buildplate_only", supports_en && is_branching_tree);
    toggle_field("branchingsupport_base_diameter", supports_en && is_branching_tree);
    toggle_field("branchingsupport_base_height", supports_en && is_branching_tree);
    toggle_field("branchingsupport_base_safety_distance", supports_en && is_branching_tree);
    toggle_field("branchingsupport_critical_angle", supports_en && is_branching_tree);
    toggle_field("branchingsupport_max_bridge_length", supports_en && is_branching_tree);
    toggle_field("branchingsupport_max_pillar_link_distance", false);
    toggle_field("branchingsupport_pillar_widening_factor", supports_en && is_branching_tree);
    toggle_field("branchingsupport_max_weight_on_model", supports_en && is_branching_tree);

    toggle_field("support_points_density_relative", supports_en);

    bool pad_en = config->opt_bool("pad_enable");

    toggle_field("pad_wall_thickness", pad_en);
    toggle_field("pad_wall_height", pad_en);
    toggle_field("pad_brim_size", pad_en);
    toggle_field("pad_max_merge_distance", pad_en);
    // toggle_field("pad_edge_radius", supports_en);
    toggle_field("pad_wall_slope", pad_en);
    toggle_field("pad_around_object", pad_en);
    toggle_field("pad_around_object_everywhere", pad_en);

    bool zero_elev = config->opt_bool("pad_around_object") && pad_en;

    toggle_field("support_object_elevation", supports_en && is_default_tree && !zero_elev);
    toggle_field("branchingsupport_object_elevation", supports_en && is_branching_tree && !zero_elev);
    toggle_field("pad_object_gap", zero_elev);
    toggle_field("pad_around_object_everywhere", zero_elev);
    toggle_field("pad_object_connector_stride", zero_elev);
    toggle_field("pad_object_connector_width", zero_elev);
    toggle_field("pad_object_connector_penetration", zero_elev);
}

} // namespace GUI
} // namespace Slic3r
