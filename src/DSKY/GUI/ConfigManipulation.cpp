///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2019 - 2023 Lukáš Hejl @hejllukas, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Oleksandra Iushchenko @YuSanka, Pavel Mikuš @Godrak, Tomáš Mészáros @tamasmeszaros
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "ConfigManipulation.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "format.hpp"
#include "luminary/model/scene/Model.hpp"
#include "luminary/presets/bundle/PresetBundle.hpp"
#include "luminary/layer/settings_spec/SettingsSpec.hpp"
#include "MsgDialog.hpp"

#include <cmath>
#include <map>
#include <set>
#include <string>
#include <wx/msgdlg.h>

namespace DSKY
{
using namespace Luminary;

// These track widths that have been explicitly approved by the user to suppress validation warnings
static std::map<std::string, double> s_approved_narrow_widths; // below nozzle_width_warning_min
static std::map<std::string, double> s_approved_wide_widths;   // above nozzle_width_warning_max

// Suppress all config validation dialogs during startup/GUI recreation.
// Modal dialogs during load_current_presets() deadlock behind the splash screen.
static bool s_suppress_startup_dialogs = false;

void ConfigManipulation::set_suppress_startup_dialogs(bool suppress)
{
    s_suppress_startup_dialogs = suppress;
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

void ConfigManipulation::toggle_field(const std::string &opt_key, const bool toggle, int opt_index /* = -1*/,
                                      const wxString &reason /* = wxString()*/)
{
    if (local_config)
    {
        if (local_config->option(opt_key) == nullptr)
            return;
    }
    cb_toggle_field(opt_key, toggle, opt_index, reason);
}

// One pass of the row specification's rules over the config: each state names the row's key,
// its extruder (-1 for a row that is not per extruder) and, when disabled, the reason.
void ConfigManipulation::toggle_rule_states(unsigned preset, DynamicPrintConfig *config, size_t extruders_count)
{
    for (const ToggleState &state : apply_toggle_rules(preset, *config, extruders_count))
        toggle_field(state.key, state.enabled, state.extruder, state.enabled ? wxString() : _(state.reason));
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

void ConfigManipulation::update_print_fff_config(DynamicPrintConfig *config, const bool is_global_config,
                                                 const std::string &changed_opt_key)
{
    //! Temporary workaround for the correct updates of the TextCtrl (like "layer_height"):
    // KillFocus() for the wxSpinCtrl use CallAfter function. So,
    // to except the duplicate call of the update() after dialog->ShowModal(),
    // let check if this process is already started.
    if (is_msg_dlg_already_exist)
        return;

    // Skip all validation dialogs during startup - presets were validated when saved,
    // and modal dialogs here would deadlock behind the splash screen.
    if (s_suppress_startup_dialogs)
        return;

    // Determine if we should validate extrusion widths
    // Only validate when the changed key is an extrusion width or nozzle_diameter
    static const std::set<std::string> extrusion_width_keys = {"extrusion_width",
                                                               "first_layer_extrusion_width",
                                                               "perimeter_extrusion_width",
                                                               "external_perimeter_extrusion_width",
                                                               "infill_extrusion_width",
                                                               "solid_infill_extrusion_width",
                                                               "top_infill_extrusion_width",
                                                               "support_material_extrusion_width",
                                                               "support_material_interface_extrusion_width",
                                                               "bridge_extrusion_width",
                                                               "nozzle_diameter"};
    bool should_validate_extrusion_widths = changed_opt_key.empty() || extrusion_width_keys.count(changed_opt_key) > 0;

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

    // Layer height must stay below the nozzle diameter. A typo such as "25"
    // instead of "0.25" otherwise reaches the flow math, where width - height*0.2146 goes
    // negative and throws FlowErrorNegativeSpacing deep inside slicing/preview. Clamp here.
    const DynamicPrintConfig &printer_config = wxGetApp().preset_bundle->printers.get_selected_preset().config;
    double max_nozzle_diam = 0.4;
    if (auto *nozzle_opt = printer_config.option<ConfigOptionFloats>("nozzle_diameter");
        nozzle_opt && !nozzle_opt->values.empty())
    {
        max_nozzle_diam = nozzle_opt->values.front();
        for (double d : nozzle_opt->values)
            if (d > max_nozzle_diam)
                max_nozzle_diam = d;
    }
    const double safe_layer_height = 0.5 * max_nozzle_diam;

    if (config->opt_float("layer_height") > max_nozzle_diam)
    {
        const wxString msg_text =
            wxString::Format(_L("Layer height must be smaller than the nozzle diameter (%.2f mm).\n\nThe layer "
                                "height will be reset to %.2f."),
                             max_nozzle_diam, safe_layer_height);
        MessageDialog dialog(m_msg_dlg_parent, msg_text, _L("Layer height"), wxICON_WARNING | wxOK);
        DynamicPrintConfig new_conf = *config;
        is_msg_dlg_already_exist = true;
        dialog.ShowModal();
        new_conf.set_key_value("layer_height", new ConfigOptionFloat(safe_layer_height));
        apply(config, &new_conf);
        is_msg_dlg_already_exist = false;
    }

    // Only an absolute first layer height can blow up the flow math; a percentage is relative
    // to the (now clamped) layer height and is bounded with it.
    if (auto *flh_opt = config->option<ConfigOptionFloatOrPercent>("first_layer_height");
        flh_opt && !flh_opt->percent && flh_opt->value > max_nozzle_diam)
    {
        const wxString msg_text =
            wxString::Format(_L("First layer height must be smaller than the nozzle diameter (%.2f mm).\n\nThe "
                                "first layer height will be reset to %.2f."),
                             max_nozzle_diam, safe_layer_height);
        MessageDialog dialog(m_msg_dlg_parent, msg_text, _L("First layer height"), wxICON_WARNING | wxOK);
        DynamicPrintConfig new_conf = *config;
        is_msg_dlg_already_exist = true;
        dialog.ShowModal();
        new_conf.set_key_value("first_layer_height", new ConfigOptionFloatOrPercent(safe_layer_height, false));
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

    // State for "Yes to All" / "No to All" across all extrusion width validations
    bool approve_all_widths = false;
    bool reset_all_widths = false;

    // Helper lambda to validate extrusion width against its corresponding nozzle
    auto validate_extrusion_width =
        [&](const std::string &width_key, const std::string &extruder_key, const std::string &label)
    {
        if (is_msg_dlg_already_exist)
            return;

        // Skip validation if the changed key is not related to extrusion widths
        // This prevents warnings when user changes unrelated settings like "perimeters"
        if (!should_validate_extrusion_widths)
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

        // Read per-nozzle warning thresholds from printer config
        auto *warn_min_opt = printer_config.option<ConfigOptionPercents>("nozzle_width_warning_min");
        auto *warn_max_opt = printer_config.option<ConfigOptionPercents>("nozzle_width_warning_max");
        double warn_min_pct = warn_min_opt ? warn_min_opt->get_at(extruder_idx) : 60.0;
        double warn_max_pct = warn_max_opt ? warn_max_opt->get_at(extruder_idx) : 150.0;
        double min_width = nozzle_diam * warn_min_pct / 100.0;
        double max_width = nozzle_diam * warn_max_pct / 100.0;
        // The value is the user's own, so any overrun is reported; the 1e-5 mm tolerance only
        // absorbs arithmetic noise at exact equality. Generated widths, which the slicing-time
        // check compares as whole percentages, get more slack than a typed value does.
        bool is_too_narrow = width_mm < min_width - 1e-5;
        bool is_too_wide = width_mm > max_width + 1e-5;

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
            msg_text =
                wxString::Format(_L("%s is set to %s, which is below %.0f%% of the nozzle diameter (%.2f mm).\n\n"
                                    "Extrusion widths below %.0f%% of nozzle size may cause printing issues.\n\n"
                                    "Do you want to keep this value?\n"
                                    "Select YES to keep %s,\n"
                                    "or NO to reset to %.2f mm (nozzle diameter)."),
                                 label, width_str, warn_min_pct, nozzle_diam, warn_min_pct, width_str, nozzle_diam);
        }
        else
        {
            msg_text = wxString::Format(_L("%s is set to %s, which exceeds %.0f%% of the nozzle diameter (%.2f mm).\n\n"
                                           "Extrusion widths above %.0f%% of nozzle size may cause printing issues.\n\n"
                                           "Do you want to keep this value?\n"
                                           "Select YES to keep %s,\n"
                                           "or NO to reset to %.2f mm (nozzle diameter)."),
                                        label, width_str, warn_max_pct, nozzle_diam, warn_max_pct, width_str,
                                        nozzle_diam);
        }

        // Handle "to All" state from a previous iteration
        bool keep = false;
        if (approve_all_widths)
            keep = true;
        else if (reset_all_widths)
            keep = false;
        else
        {
            // Show 4-button dialog: Yes / Yes to All / No / No to All
            enum
            {
                ID_YES_TO_ALL = wxID_HIGHEST + 1,
                ID_NO_TO_ALL
            };

            wxDialog dialog(m_msg_dlg_parent, wxID_ANY, _L("Parameter validation") + ": " + width_key,
                            wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE);
            wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
            wxBoxSizer *btn_sizer = new wxBoxSizer(wxHORIZONTAL);

            sizer->Add(new wxStaticText(&dialog, wxID_ANY, msg_text), 0, wxALL, 15);

            btn_sizer->AddStretchSpacer();
            auto add_btn = [&](wxWindowID id, const wxString &lbl, bool focus)
            {
                wxButton *btn = new wxButton(&dialog, id, lbl);
                if (focus)
                {
                    btn->SetFocus();
                    btn->SetDefault();
                }
                btn_sizer->Add(btn, 0, wxLEFT, 5);
                btn->Bind(wxEVT_BUTTON, [&dialog, id](wxCommandEvent &) { dialog.EndModal(id); });
            };
            add_btn(wxID_YES, _L("Yes"), true);
            add_btn(ID_YES_TO_ALL, _L("Yes to All"), false);
            add_btn(wxID_NO, _L("No"), false);
            add_btn(ID_NO_TO_ALL, _L("No to All"), false);

            sizer->Add(btn_sizer, 0, wxEXPAND | wxALL, 10);
            dialog.SetSizerAndFit(sizer);
            wxGetApp().UpdateDlgDarkUI(&dialog);
            dialog.CenterOnParent();

            is_msg_dlg_already_exist = true;
            int result = dialog.ShowModal();
            is_msg_dlg_already_exist = false;

            if (result == ID_YES_TO_ALL)
            {
                approve_all_widths = true;
                keep = true;
            }
            else if (result == ID_NO_TO_ALL)
            {
                reset_all_widths = true;
                keep = false;
            }
            else
                keep = (result == wxID_YES);
        }

        if (keep)
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
          !config->opt_bool("interlock_perimeters_enabled")))
    {
        wxString msg_text = _(L("The Spiral Vase mode requires:\n"
                                "- one perimeter\n"
                                "- no top solid layers\n"
                                "- 0% fill density\n"
                                "- no support material\n"
                                "- Interlocking perimeters disabled"));
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
            new_conf.set_key_value("interlock_perimeters_enabled", new ConfigOptionBool(false));
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
        // Organic and Baobab supports are always synchronized with object layers as of now.
        config->opt_enum<SupportMaterialStyle>("support_material_style") != smsOrganic &&
        config->opt_enum<SupportMaterialStyle>("support_material_style") != smsBaobab)
    {
        // Sparse support layers follow the object layer heights, interface layers near top contacts
        // take their own heights, and the wipe tower takes every support z from the tool ordering,
        // so only the non-soluble case is constrained.
        if (config->opt_enum<SupportTopContactGap>("support_material_contact_distance") != stcgNoGap)
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
                wxString msg_text = DSKY::format_wxstr(
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
    toggle_rule_states(SettingPresetPrint, config, 0);

    // Clamp interlock_regular_perimeters to not exceed perimeters
    if (config->opt_bool("interlock_perimeters_enabled"))
    {
        int il_reg = config->opt_int("interlock_regular_perimeters");
        int perims = config->opt_int("perimeters");
        if (il_reg > perims && il_reg > 0)
        {
            DynamicPrintConfig new_conf = *config;
            new_conf.set_key_value("interlock_regular_perimeters", new ConfigOptionInt(perims));
            apply(config, &new_conf);
        }
    }
}

void ConfigManipulation::toggle_filament_options(DynamicPrintConfig *config)
{
    toggle_rule_states(SettingPresetFilament, config, 0);
}

void ConfigManipulation::toggle_printer_options(DynamicPrintConfig *config, size_t extruders_count)
{
    toggle_rule_states(SettingPresetPrinter, config, extruders_count);
}

} // namespace DSKY
