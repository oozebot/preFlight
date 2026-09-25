///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2017 - 2023 Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Pavel Mikuš @Godrak, David Kocík @kocikdav, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966, Lukáš Hejl @hejllukas, Filip Sykala @Jony01, Vojtěch Král @vojtechkral
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "luminary/core/Prelude.hpp"
#include "luminary/platform/paths/Paths.hpp"
#include "luminary/platform/files/FileIO.hpp"
#include "luminary/platform/process/Process.hpp"
#include "AppConfig.hpp"
#include "luminary/core/Exception.hpp"
#include "LocalesUtils.hpp"
#include "luminary/platform/concurrency/Thread.hpp"
#include "luminary/core/format.hpp"

#include <utility>
#include <vector>
#include <stdexcept>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree_fwd.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/format/format_fwd.hpp>
#include <boost/log/trivial.hpp>

#ifdef WIN32
#include <boost/uuid/detail/md5.hpp>
#include <boost/algorithm/hex.hpp>
#endif

namespace Luminary
{

static const std::string VENDOR_PREFIX = "vendor:";
static const std::string MODEL_PREFIX = "model:";
// Because of a crash in preFlight 2.3.0/2.3.1 when showing an update notification with some locales, we don't want preFlight 2.3.0/2.3.1
// to show this notification. On the other hand, we would like preFlight 2.3.2 to show an update notification of the upcoming preFlight 2.4.0.
// Thus we will let preFlight 2.3.2 and couple of follow-up versions to download the version number from an alternate file until the preFlight 2.3.0/2.3.1
// are phased out, then we will revert to the original name.
// For 2.6.0-alpha1 we have switched back to the original. The file should contain data for AppUpdater.cpp
// Version check URL removed
static const std::string VERSION_CHECK_URL = "https://preflight3d.com/latest.version";
static const std::string RELEASE_NOTES_URL = "https://preflight3d.com/release.notes";
// Url to index archive zip that contains latest indicies
// Index archive URL removed
static const std::string INDEX_ARCHIVE_URL = ""; // Disabled for preFlight
// Url to folder with vendor profile files. Used when downloading new profiles that are not in resources folder.
// Profile folder URL removed
static const std::string PROFILE_FOLDER_URL = ""; // Disabled for preFlight

const std::string AppConfig::SECTION_FILAMENTS = "filaments";
const std::string AppConfig::SECTION_EMBOSS_STYLE = "font";

void AppConfig::reset()
{
    m_storage.clear();
    m_vendors.clear();
    m_dirty = false;
    m_orig_version = Semver::invalid();
    m_legacy_datadir = false;
    set_defaults();
};

// Override missing or keys with their defaults.
void AppConfig::set_defaults()
{
    if (m_mode == EAppMode::Editor)
    {
        // Background processing is retired: the Preview re-slices in place. Drop the stale key.
        erase("", "background_processing");
        // Preview corner: "1" shows the sidebar, "0" shows the legend. The sidebar shows first so its
        // presence in the Preview is seen before the legend replaces it.
        if (get("preview_sidebar").empty())
            set("preview_sidebar", "1");
        // Enable support issues alerts by default
        if (get("alert_when_supports_needed").empty())
            set("alert_when_supports_needed", "1");
        // If set, the "Controller" tab for the control of the printer over serial line and the serial port settings are hidden.
        // By default, the controller is hidden.
        if (get("no_controller").empty())
            set("no_controller", "1");
        // If set, the "- default -" selections of print/filament/printer are suppressed, if there is a valid preset available.
        if (get("no_defaults").empty())
            set("no_defaults", "1");
        if (get("show_incompatible_presets").empty())
            set("show_incompatible_presets", "0");

        if (get("show_drop_project_dialog").empty())
            set("show_drop_project_dialog", "1");
        if (get("drop_project_action").empty())
            set("drop_project_action", "1");

        if (get("export_sources_full_pathnames").empty())
            set("export_sources_full_pathnames", "0");

#ifdef _WIN32
        if (get("associate_3mf").empty())
            set("associate_3mf", "0");
        if (get("associate_stl").empty())
            set("associate_stl", "0");

        if (get("suppress_round_corners").empty())
            set("suppress_round_corners", "1");
#endif // _WIN32

        // remove old 'use_legacy_opengl' parameter from this config, if present
        if (!get("use_legacy_opengl").empty())
            erase("", "use_legacy_opengl");

        if (get("single_instance").empty())
            set("single_instance",
#ifdef __APPLE__
                "1"
#else  // __APPLE__
                "0"
#endif // __APPLE__
            );

        if (get("preprocessing_consent_accepted").empty())
            set("preprocessing_consent_accepted", "0");
        if (get("preprocessing_category_order").empty())
            set("preprocessing_category_order", "print,filament,printer");

        if (get("remember_output_path").empty())
            set("remember_output_path", "1");

        if (get("remember_output_path_removable").empty())
            set("remember_output_path_removable", "1");

        if (get("use_tabbed_sidebar").empty())
            set("use_tabbed_sidebar", "1");

        if (get("legacy_prepare_layout").empty())
            set("legacy_prepare_layout", "0");

        if (get("use_custom_toolbar_size").empty())
            set("use_custom_toolbar_size", "0");

        if (get("custom_toolbar_size").empty())
            set("custom_toolbar_size", "100");

        if (get("auto_toolbar_size").empty())
            set("auto_toolbar_size", "100");

        if (get("use_binary_gcode_when_supported").empty())
            set("use_binary_gcode_when_supported", "1");

        if (get("notify_release").empty())
            set("notify_release", "all"); // or "none" or "release"

        // "auto" detects GPU at startup: discrete -> "enhanced", integrated -> "basic".
        // "full" (shadows + ambient occlusion + PBR) is a deliberate opt-in and is
        // never chosen automatically.
        if (get("canvas_lighting_quality").empty())
            set("canvas_lighting_quality", "auto");

        // MSAA sample count: "auto" tries highest available, "0" disables
        if (get("canvas_msaa").empty())
            set("canvas_msaa", "auto");

        // Which mouse buttons orbit/pan the 3D view: default, blender, fusion,
        // solidworks or tinkercad
        if (get("canvas_mouse_scheme").empty())
            set("canvas_mouse_scheme", "default");

        // Supersampled scene rendering: "off", "1.5" or "2" (render scale)
        if (get("canvas_ssaa_scale").empty())
            set("canvas_ssaa_scale", "off");

        if (get("preview_detail").empty())
        {
#if defined(__linux__) && defined(__aarch64__)
            set("preview_detail", "1000000");
#else
            set("preview_detail", "10000000");
#endif
        }

        if (get("use_inches").empty())
            set("use_inches", "0");

        if (get("default_action_on_close_application").empty())
            set("default_action_on_close_application", "none"); // , "discard" or "save"

        if (get("default_action_on_select_preset").empty())
            set("default_action_on_select_preset", "none"); // , "transfer", "discard" or "save"

        if (get("default_action_on_new_project").empty())
            set("default_action_on_new_project", "none"); // , "keep(transfer)", "discard" or "save"

        if (get("color_manipulation_panel").empty())
            set("color_manipulation_panel", "0");

        if (get("order_volumes").empty())
            set("order_volumes", "1");

        if (get("non_manifold_edges").empty())
            set("non_manifold_edges", "1");

        if (get("clear_undo_redo_stack_on_new_project").empty())
            set("clear_undo_redo_stack_on_new_project", "1");

        // CPU stability preferences: 0 = full speed / no restriction.
        if (get("cpu_max_slicing_threads").empty())
            set("cpu_max_slicing_threads", "0");
        if (get("cpu_pcores_only").empty())
            set("cpu_pcores_only", "0");
        if (get("cpu_nvidia_disable_threaded_opt").empty())
            set("cpu_nvidia_disable_threaded_opt", "0");
        // Drop the superseded key from pre-rename builds.
        if (!get("cpu_windows_pcores_only").empty())
            erase("", "cpu_windows_pcores_only");
    }
    else
    {
#ifdef _WIN32
        if (get("associate_gcode").empty())
            set("associate_gcode", "0");
        if (get("associate_bgcode").empty())
            set("associate_bgcode", "0");
#endif // _WIN32
    }

#ifdef __APPLE__
    if (get("use_retina_opengl").empty())
        set("use_retina_opengl", "1");
#endif // __APPLE__

    // Preview legend toggle defaults (0 = hidden, 1 = visible)
    if (get("preview_travel").empty())
        set("preview_travel", "0");
    if (get("preview_wipe").empty())
        set("preview_wipe", "0");
    if (get("preview_retractions").empty())
        set("preview_retractions", "0");
    if (get("preview_unretractions").empty())
        set("preview_unretractions", "0");
    if (get("preview_seams").empty())
        set("preview_seams", "0");
    if (get("preview_toolchanges").empty())
        set("preview_toolchanges", "0");
    if (get("preview_colorchanges").empty())
        set("preview_colorchanges", "0");
    if (get("preview_pauseprints").empty())
        set("preview_pauseprints", "0");
    if (get("preview_customgcodes").empty())
        set("preview_customgcodes", "0");
    if (get("preview_cog").empty())
        set("preview_cog", "0");
    if (get("preview_shells").empty())
        set("preview_shells", "0");
    if (get("preview_toolmarker").empty())
        set("preview_toolmarker", "1");

    if (get("use_perspective_camera").empty())
        set("use_perspective_camera", "1");

    if (get("use_free_camera").empty())
        set("use_free_camera", "0");

    if (get("reverse_mouse_wheel_zoom").empty())
        set("reverse_mouse_wheel_zoom", "0");

    if (get("show_splash_screen").empty())
        set("show_splash_screen", "1");

    if (get("restore_win_position").empty())
        set("restore_win_position", "1"); // allowed values - "1", "0", "crashed_at_..."

    if (get("show_hints").empty())
        set("show_hints", "1");

    if (get("allow_auto_color_change").empty())
        set("allow_auto_color_change", "1");

    if (get("allow_ip_resolve").empty())
        set("allow_ip_resolve", "1");

    if (get("connect_polling").empty())
        set("connect_polling", "1");

    if (get("printer_webview_external_browser").empty())
        set("printer_webview_external_browser", "0");

    if (get("auth_login_dialog_confirmed").empty())
        set("auth_login_dialog_confirmed", "0");

    if (get("show_estimated_times_in_dbl_slider").empty())
        set("show_estimated_times_in_dbl_slider", "1");

    if (get("show_ruler_in_dbl_slider").empty())
        set("show_ruler_in_dbl_slider", "1");

    if (get("show_ruler_bg_in_dbl_slider").empty())
        set("show_ruler_bg_in_dbl_slider", "1");

#ifdef _WIN32
    if (get("use_legacy_3DConnexion").empty())
        set("use_legacy_3DConnexion", "0");
#endif // _WIN32

    if (get("show_step_import_parameters").empty())
        set("show_step_import_parameters", "1");

    // The documentation-hyperlink feature is not implemented; force-suppress on every
    // launch so parameter labels never render as links. Restore the empty-check when the feature lands.
    set("suppress_hyperlinks", "1");

    if (get("linear_precision").empty())
        set("linear_precision", "0.005");

    if (get("angle_precision").empty())
        set("angle_precision", "1.");

    // Remove legacy window positions/sizes
    erase("", "main_frame_maximized");
    erase("", "main_frame_pos");
    erase("", "main_frame_size");
    erase("", "object_settings_maximized");
    erase("", "object_settings_pos");
    erase("", "object_settings_size");

    // These settings are hardcoded and cannot be changed by the user
    set("show_splash_screen", "1");       // Force enabled
    set("show_collapse_button", "0");     // Force disabled
    set("color_manipulation_panel", "0"); // Force disabled
    set("use_custom_toolbar_size", "0");  // Force disabled (uses 100%)
    set("custom_toolbar_size", "100");    // Force 100%
    set("old_settings_layout_mode", "1"); // Force old regular layout
    set("dlg_settings_layout_mode", "0"); // Force NOT dialog layout
}

#ifdef WIN32
static std::string appconfig_md5_hash_line(const std::string_view data)
{
    // boost::uuids::detail::md5 is an internal boost namespace and may change between boost versions.
    using boost::uuids::detail::md5;
    md5 md5_hash;
    // unsigned int[4], 128 bits
    md5::digest_type md5_digest{};
    std::string md5_digest_str;
    md5_hash.process_bytes(data.data(), data.size());
    md5_hash.get_digest(md5_digest);
    boost::algorithm::hex(md5_digest, md5_digest + std::size(md5_digest), std::back_inserter(md5_digest_str));
    // MD5 hash is 32 HEX digits long.
    assert(md5_digest_str.size() == 32);
    // This line will be emited at the end of the file.
    return "# MD5 checksum " + md5_digest_str + "\n";
};

struct ConfigFileInfo
{
    bool correct_checksum{false};
    bool contains_null{false};
};

// Assume that the last line with the comment inside the config file contains a checksum and that the user didn't modify the config file.
static ConfigFileInfo check_config_file_and_verify_checksum(boost::nowide::ifstream &ifs)
{
    auto read_whole_config_file = [&ifs]() -> std::string
    {
        std::stringstream ss;
        ss << ifs.rdbuf();
        return ss.str();
    };

    ifs.seekg(0, boost::nowide::ifstream::beg);
    const std::string whole_config = read_whole_config_file();
    const bool contains_null = whole_config.find_first_of('\0') != std::string::npos;

    // The checksum should be on the last line in the config file.
    if (size_t last_comment_pos = whole_config.find_last_of('#'); last_comment_pos != std::string::npos)
    {
        // Split read config into two parts, one with checksum, and the second part is part with configuration from the checksum was computed.
        // Verify existence and validity of the MD5 checksum line at the end of the file.
        // When the checksum isn't found, the checksum was not saved correctly, it was removed or it is an older config file without the checksum.
        // If the checksum is incorrect, then the file was either not saved correctly or modified.
        if (std::string_view(whole_config.c_str() + last_comment_pos, whole_config.size() - last_comment_pos) ==
            appconfig_md5_hash_line({whole_config.data(), last_comment_pos}))
            return {true, contains_null};
    }
    return {false, contains_null};
}
#endif

std::string AppConfig::load(const std::string &path)
{
    this->reset();

    // 1) Read the complete config file into a boost::property_tree.
    namespace pt = boost::property_tree;
    pt::ptree tree;
    boost::nowide::ifstream ifs;
    bool recovered = false;

    try
    {
        ifs.open(path);
#ifdef WIN32
        // Verify the checksum of the config file without taking just for debugging purpose.
        const ConfigFileInfo config_file_info = check_config_file_and_verify_checksum(ifs);
        if (!config_file_info.correct_checksum)
            BOOST_LOG_TRIVIAL(info)
                << "The configuration file " << path
                << " has a wrong MD5 checksum or the checksum is missing. This may indicate a file corruption or a harmless user edit.";

        if (!config_file_info.correct_checksum && config_file_info.contains_null)
        {
            BOOST_LOG_TRIVIAL(info) << "The configuration file " + path +
                                           " is corrupted, because it is contains null characters.";
            throw Luminary::CriticalException("The configuration file contains null characters.");
        }

        ifs.seekg(0, boost::nowide::ifstream::beg);
#endif
        try
        {
            pt::read_ini(ifs, tree);
        }
        catch (pt::ptree_error &ex)
        {
            throw Luminary::CriticalException(ex.what());
        }
    }
    catch (Luminary::CriticalException &ex)
    {
#ifdef WIN32
        // The configuration file is corrupted, try replacing it with the backup configuration.
        ifs.close();
        std::string backup_path = (boost::format("%1%.bak") % path).str();
        if (boost::filesystem::exists(backup_path))
        {
            // Compute checksum of the configuration backup file and try to load configuration from it when the checksum is correct.
            boost::nowide::ifstream backup_ifs(backup_path);
            if (const ConfigFileInfo config_file_info = check_config_file_and_verify_checksum(backup_ifs);
                !config_file_info.correct_checksum || config_file_info.contains_null)
            {
                BOOST_LOG_TRIVIAL(error) << format(
                    R"(Both "%1%" and "%2%" are corrupted. It isn't possible to restore configuration from the backup.)",
                    path, backup_path);
                backup_ifs.close();
                boost::filesystem::remove(backup_path);
            }
            else if (std::string error_message; copy_file(backup_path, path, error_message, false) != SUCCESS)
            {
                BOOST_LOG_TRIVIAL(error) << format(
                    R"(Configuration file "%1%" is corrupted. Failed to restore from backup "%2%": %3%)", path,
                    backup_path, error_message);
                backup_ifs.close();
                boost::filesystem::remove(backup_path);
            }
            else
            {
                BOOST_LOG_TRIVIAL(info) << format(
                    R"(Configuration file "%1%" was corrupted. It has been successfully restored from the backup "%2%".)",
                    path, backup_path);
                // Try parse configuration file after restore from backup.
                try
                {
                    ifs.open(path);
                    pt::read_ini(ifs, tree);
                    recovered = true;
                }
                catch (pt::ptree_error &ex)
                {
                    BOOST_LOG_TRIVIAL(info) << format(
                        R"(Failed to parse configuration file "%1%" after it has been restored from backup: %2%)", path,
                        ex.what());
                }
            }
        }
        else
#endif // WIN32
            BOOST_LOG_TRIVIAL(info) << format(R"(Failed to parse configuration file "%1%": %2%)", path, ex.what());
        if (!recovered)
        {
            // Report the initial error of parsing preFlight.ini.
            // Error while parsing config file. We'll customize the error message and rethrow to be displayed.
            // Rethrow from the caller's context so the UI layer can display it
            return ex.what();
        }
    }

    // 2) Parse the property_tree, extract the sections and key / value pairs.
    for (const auto &section : tree)
    {
        if (section.second.empty())
        {
            // This may be a top level (no section) entry, or an empty section.
            std::string data = section.second.data();
            if (!data.empty())
                // If there is a non-empty data, then it must be a top-level (without a section) config entry.
                m_storage[""][section.first] = data;
        }
        else if (boost::starts_with(section.first, VENDOR_PREFIX))
        {
            // This is a vendor section listing enabled model / variants
            const auto vendor_name = section.first.substr(VENDOR_PREFIX.size());
            auto &vendor = m_vendors[vendor_name];
            for (const auto &kvp : section.second)
            {
                if (!boost::starts_with(kvp.first, MODEL_PREFIX))
                {
                    continue;
                }
                const auto model_name = kvp.first.substr(MODEL_PREFIX.size());
                std::vector<std::string> variants;
                if (!unescape_strings_cstyle(kvp.second.data(), variants))
                {
                    continue;
                }
                for (const auto &variant : variants)
                {
                    vendor[model_name].insert(variant);
                }
            }
        }
        else
        {
            // This must be a section name. Read the entries of a section.
            std::map<std::string, std::string> &storage = m_storage[section.first];
            for (auto &kvp : section.second)
                storage[kvp.first] = kvp.second.data();
        }
    }

    // Figure out if datadir has legacy presets
    auto ini_ver = Semver::parse(get("version"));
    m_legacy_datadir = false;
    if (ini_ver)
    {
        m_orig_version = *ini_ver;
        // Make 1.40.0 alphas compare well
        ini_ver->set_metadata(boost::none);
        ini_ver->set_prerelease(boost::none);
        m_legacy_datadir = ini_ver < Semver(1, 40, 0);
    }

    // Legacy conversion
    if (m_mode == EAppMode::Editor)
    {
        // Convert [extras] "physical_printer" to [presets] "physical_printer",
        // remove the [extras] section if it becomes empty.
        if (auto it_section = m_storage.find("extras"); it_section != m_storage.end())
        {
            if (auto it_physical_printer = it_section->second.find("physical_printer");
                it_physical_printer != it_section->second.end())
            {
                m_storage["presets"]["physical_printer"] = it_physical_printer->second;
                it_section->second.erase(it_physical_printer);
            }
            if (it_section->second.empty())
                m_storage.erase(it_section);
        }
    }

    // Override missing or keys with their defaults.
    this->set_defaults();
    m_dirty = false;
    return "";
}

std::string AppConfig::load()
{
    return this->load(AppConfig::config_path());
}

void AppConfig::save()
{
    if (!is_main_thread_active())
        throw CriticalException("Calling AppConfig::save() from a worker thread!");

    // The config is first written to a file with a PID suffix and then moved
    // to avoid race conditions with multiple instances of the application
    const auto path = config_path();
    std::string path_pid = (boost::format("%1%.%2%") % path % get_current_pid()).str();

    std::stringstream config_ss;
    if (m_mode == EAppMode::Editor)
        config_ss << "# " << Luminary::header_generated() << std::endl;
    else
        config_ss << "# " << Luminary::header_gcodeviewer_generated() << std::endl;
    // Make sure the "no" category is written first.
    for (const auto &kvp : m_storage[""])
        config_ss << kvp.first << " = " << kvp.second << std::endl;
    // Write the other categories.
    for (const auto &category : m_storage)
    {
        if (category.first.empty())
            continue;
        config_ss << std::endl << "[" << category.first << "]" << std::endl;
        for (const auto &kvp : category.second)
            config_ss << kvp.first << " = " << kvp.second << std::endl;
    }
    // Write vendor sections
    for (const auto &vendor : m_vendors)
    {
        size_t size_sum = 0;
        for (const auto &model : vendor.second)
        {
            size_sum += model.second.size();
        }
        if (size_sum == 0)
        {
            continue;
        }

        config_ss << std::endl << "[" << VENDOR_PREFIX << vendor.first << "]" << std::endl;

        for (const auto &model : vendor.second)
        {
            if (model.second.empty())
            {
                continue;
            }
            const std::vector<std::string> variants(model.second.begin(), model.second.end());
            const auto escaped = escape_strings_cstyle(variants);
            config_ss << MODEL_PREFIX << model.first << " = " << escaped << std::endl;
        }
    }
    // One empty line before the MD5 sum.
    config_ss << std::endl;

    std::string config_str = config_ss.str();
    boost::nowide::ofstream c;
    c.open(path_pid, std::ios::out | std::ios::trunc);
    c << config_str;
#ifdef WIN32
    // WIN32 specific: The final "rename_file()" call is not safe in case of an application crash, there is no atomic "rename file" API
    // provided by Windows (sic!). Therefore we save a MD5 checksum to be able to verify file corruption. In addition,
    // we save the config file into a backup first before moving it to the final destination.
    c << appconfig_md5_hash_line(config_str);
#endif
    c.close();

#ifdef WIN32
    // Make a backup of the configuration file before copying it to the final destination.
    std::string error_message;
    std::string backup_path = (boost::format("%1%.bak") % path).str();
    // Copy configuration file with PID suffix into the configuration file with "bak" suffix.
    if (copy_file(path_pid, backup_path, error_message, false) != SUCCESS)
        BOOST_LOG_TRIVIAL(error) << "Copying from " << path_pid << " to " << backup_path
                                 << " failed. Failed to create a backup configuration.";
#endif

    // Rename the config atomically.
    // On Windows, the rename is likely NOT atomic, thus it may fail if preFlight crashes on another thread in the meanwhile.
    // To cope with that, we already made a backup of the config on Windows.
    rename_file(path_pid, path);
    m_dirty = false;
}

bool AppConfig::erase(const std::string &section, const std::string &key)
{
    if (auto it_storage = m_storage.find(section); it_storage != m_storage.end())
    {
        auto &section = it_storage->second;
        auto it = section.find(key);
        if (it != section.end())
        {
            section.erase(it);
            m_dirty = true;
            return true;
        }
    }
    return false;
}

bool AppConfig::set_section(const std::string &section, std::map<std::string, std::string> data)
{
    auto it_section = m_storage.find(section);
    if (it_section == m_storage.end())
    {
        if (data.empty())
            return false;
        it_section = m_storage.insert({section, {}}).first;
    }
    auto &dst = it_section->second;
    if (dst == data)
        return false;
    dst = std::move(data);
    m_dirty = true;
    return true;
}

bool AppConfig::clear_section(const std::string &section)
{
    if (auto it_section = m_storage.find(section); it_section != m_storage.end() && !it_section->second.empty())
    {
        it_section->second.clear();
        m_dirty = true;
        return true;
    }
    return false;
}

bool AppConfig::get_variant(const std::string &vendor, const std::string &model, const std::string &variant) const
{
    const auto it_v = m_vendors.find(vendor);
    if (it_v == m_vendors.end())
    {
        return false;
    }
    const auto it_m = it_v->second.find(model);
    return it_m == it_v->second.end() ? false : it_m->second.find(variant) != it_m->second.end();
}

bool AppConfig::set_variant(const std::string &vendor, const std::string &model, const std::string &variant,
                            bool enable)
{
    if (enable)
    {
        if (get_variant(vendor, model, variant))
            return false;
        m_vendors[vendor][model].insert(variant);
    }
    else
    {
        auto it_v = m_vendors.find(vendor);
        if (it_v == m_vendors.end())
            return false;
        auto it_m = it_v->second.find(model);
        if (it_m == it_v->second.end())
            return false;
        auto it_var = it_m->second.find(variant);
        if (it_var == it_m->second.end())
            return false;
        it_m->second.erase(it_var);
    }
    // If we got here, there was an update
    m_dirty = true;
    return true;
}

bool AppConfig::set_vendors(const VendorMap &vendors)
{
    if (m_vendors != vendors)
    {
        m_vendors = vendors;
        m_dirty = true;
        return true;
    }
    else
        return false;
}

bool AppConfig::set_vendors(VendorMap &&vendors)
{
    if (m_vendors != vendors)
    {
        m_vendors = std::move(vendors);
        m_dirty = true;
        return true;
    }
    else
        return false;
}

std::string AppConfig::get_last_dir() const
{
    const auto it = m_storage.find("recent");
    if (it != m_storage.end())
    {
        {
            const auto it2 = it->second.find("skein_directory");
            if (it2 != it->second.end() && !it2->second.empty())
                return it2->second;
        }
        {
            const auto it2 = it->second.find("config_directory");
            if (it2 != it->second.end() && !it2->second.empty())
                return it2->second;
        }
    }
    return std::string();
}

std::vector<std::string> AppConfig::get_recent_projects() const
{
    std::vector<std::string> ret;
    const auto it = m_storage.find("recent_projects");
    if (it != m_storage.end())
    {
        for (const std::map<std::string, std::string>::value_type &item : it->second)
        {
            ret.push_back(item.second);
        }
    }
    return ret;
}

bool AppConfig::set_recent_projects(const std::vector<std::string> &recent_projects)
{
    static constexpr const char *section = "recent_projects";
    auto it_section = m_storage.find(section);
    if (it_section == m_storage.end())
    {
        if (recent_projects.empty())
            return false;
        it_section = m_storage.insert({std::string(section), {}}).first;
    }
    auto &dst = it_section->second;

    std::map<std::string, std::string> src;
    for (unsigned int i = 0; i < (unsigned int) recent_projects.size(); ++i)
        src[std::to_string(i + 1)] = recent_projects[i];

    if (src != dst)
    {
        dst = std::move(src);
        m_dirty = true;
        return true;
    }
    else
        return false;
}

bool AppConfig::set_mouse_device(const std::string &name, double translation_speed, double translation_deadzone,
                                 float rotation_speed, float rotation_deadzone, double zoom_speed, bool swap_yz)
{
    const std::string key = std::string("mouse_device:") + name;
    auto it_section = m_storage.find(key);
    if (it_section == m_storage.end())
        it_section = m_storage.insert({key, {}}).first;
    auto &dst = it_section->second;

    std::map<std::string, std::string> src;
    src["translation_speed"] = float_to_string_decimal_point(translation_speed);
    src["translation_deadzone"] = float_to_string_decimal_point(translation_deadzone);
    src["rotation_speed"] = float_to_string_decimal_point(rotation_speed);
    src["rotation_deadzone"] = float_to_string_decimal_point(rotation_deadzone);
    src["zoom_speed"] = float_to_string_decimal_point(zoom_speed);
    src["swap_yz"] = swap_yz ? "1" : "0";

    if (src != dst)
    {
        dst = std::move(src);
        m_dirty = true;
        return true;
    }
    else
        return false;
}

std::vector<std::string> AppConfig::get_mouse_device_names() const
{
    static constexpr const char *prefix = "mouse_device:";
    static const size_t prefix_len = strlen(prefix);
    std::vector<std::string> out;
    for (const auto &key_value_pair : m_storage)
        if (boost::starts_with(key_value_pair.first, prefix) && key_value_pair.first.size() > prefix_len)
            out.emplace_back(key_value_pair.first.substr(prefix_len));
    return out;
}

bool AppConfig::update_config_dir(const std::string &dir)
{
    return this->set("recent", "config_directory", dir);
}

bool AppConfig::update_skein_dir(const std::string &dir)
{
    if (is_shapes_dir(dir))
        return false; // do not save "shapes gallery" directory
    return this->set("recent", "skein_directory", dir);
}

std::string AppConfig::get_last_output_dir(const std::string &alt, const bool removable) const
{
    std::string s1 = (removable ? "last_output_path_removable" : "last_output_path");
    std::string s2 = (removable ? "remember_output_path_removable" : "remember_output_path");
    const auto it = m_storage.find("");
    if (it != m_storage.end())
    {
        const auto it2 = it->second.find(s1);
        const auto it3 = it->second.find(s2);
        if (it2 != it->second.end() && it3 != it->second.end() && !it2->second.empty() && it3->second == "1")
            return it2->second;
    }
    return is_shapes_dir(alt) ? get_last_dir() : alt;
}

bool AppConfig::update_last_output_dir(const std::string &dir, const bool removable)
{
    return this->set("", (removable ? "last_output_path_removable" : "last_output_path"), dir);
}

void AppConfig::reset_selections()
{
    auto it = m_storage.find("presets");
    if (it != m_storage.end())
    {
        it->second.erase("print");
        it->second.erase("filament");
        it->second.erase("printer");
        it->second.erase("physical_printer");
        m_dirty = true;
    }
}

std::string AppConfig::config_path() const
{
    std::string path =
        (m_mode == EAppMode::Editor)
            ? (boost::filesystem::path(Luminary::data_dir()) / (PREFLIGHT_APP_KEY ".ini")).make_preferred().string()
            : (boost::filesystem::path(Luminary::data_dir()) / (GCODEVIEWER_APP_KEY ".ini")).make_preferred().string();

    return path;
}

std::string AppConfig::version_check_url() const
{
    auto from_settings = get("version_check_url");
    return from_settings.empty() ? VERSION_CHECK_URL : from_settings;
}

std::string AppConfig::release_notes_url() const
{
    return RELEASE_NOTES_URL;
}

std::string AppConfig::index_archive_url() const
{
    return INDEX_ARCHIVE_URL;
}

std::string AppConfig::profile_folder_url() const
{
    return PROFILE_FOLDER_URL;
}

bool AppConfig::exists() const
{
    return boost::filesystem::exists(config_path());
}

}; // namespace Luminary
