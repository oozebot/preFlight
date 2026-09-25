///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "ThemePalette.hpp"

#include <wx/string.h>

#include "luminary/platform/paths/Paths.hpp"
#include "luminary/presets/app_config/AppConfig.hpp"
#include "GUI.hpp" // get_app_config()
#include "GUI_App.hpp"
#include "GUI_Utils.hpp" // check_dark_mode()

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/log/trivial.hpp>

#include "nlohmann/json.hpp"

#include <algorithm>

namespace fs = boost::filesystem;
using json = nlohmann::json;

namespace DSKY
{
using namespace Luminary;

static ThemePalette &mutable_palette()
{
    static ThemePalette palette;
    return palette;
}

static bool &mutable_is_dark()
{
    static bool is_dark = true;
    return is_dark;
}

// Pending "the selected theme could not be loaded" message. Set by load_active_theme() on failure and
// drained by the GUI once the notification system exists (the palette loads before it does at startup).
static std::string &mutable_theme_load_error()
{
    static std::string err;
    return err;
}

const ThemePalette &active_palette()
{
    return mutable_palette();
}

bool active_theme_is_dark()
{
    return mutable_is_dark();
}

std::string take_theme_load_error()
{
    std::string e = mutable_theme_load_error();
    mutable_theme_load_error().clear();
    return e;
}

ThemePalette default_light_palette()
{
    // Compiled-in "Default Light" theme (the dark default is a plain default-constructed ThemePalette).
    ThemePalette p;
    p.name = default_light_key();
    p.is_dark = false;

    // Neutral-cool light palette: pure-white content layered over cool grays for depth (the prior
    // flat warm cream lacked hierarchy). The interactive accents stay preFlight orange so this default
    // remains brand-distinct from the bundled GitHub Light theme (which uses blue).
    p.input_background = wxColour(255, 255, 255);
    p.input_background_disabled = wxColour(246, 248, 250);
    p.input_foreground = wxColour(31, 35, 40);
    p.input_foreground_disabled = wxColour(140, 149, 159);
    p.panel_background = wxColour(246, 248, 250);
    p.panel_foreground = wxColour(31, 35, 40);
    p.content_background = wxColour(255, 255, 255);
    p.content_foreground = wxColour(31, 35, 40);
    p.secondary_text = wxColour(101, 109, 118);
    p.label_default = wxColour(31, 35, 40);
    p.highlight_label = wxColour(36, 41, 47);
    p.highlight_background = wxColour(234, 238, 242); // cool neutral selection (matches section headers)
    p.hovered_btn_label = wxColour(252, 77, 1);
    p.default_btn_label = wxColour(203, 61, 0);
    p.selected_btn_background = wxColour(222, 227, 234); // cool neutral
    p.header_background = wxColour(246, 248, 250);
    p.header_hover = wxColour(234, 238, 242);
    p.section_header_background = wxColour(234, 238, 242);
    p.section_header_hover = wxColour(225, 230, 235);
    p.header_divider = wxColour(208, 215, 222);
    p.tab_background_normal = wxColour(246, 248, 250);
    p.tab_background_hover = wxColour(234, 238, 242);
    p.tab_background_selected = wxColour(255, 255, 255);
    p.tab_background_disabled = wxColour(234, 238, 242);
    p.tab_text_normal = wxColour(101, 109, 118);
    p.tab_text_selected = wxColour(31, 35, 40);
    p.tab_text_disabled = wxColour(140, 149, 159);
    p.tab_border = wxColour(208, 215, 222);
    p.static_box_border = wxColour(208, 215, 222);
    p.section_border = wxColour(31, 35, 40);
    p.canvas_background = wxColour(216, 222, 228);
    p.canvas_gradient_top = wxColour(232, 236, 240);
    p.bed_surface = wxColour(200, 208, 216);
    p.bed_grid = wxColour(174, 184, 194);
    p.menu_background = wxColour(246, 248, 250);
    p.menu_hover = wxColour(234, 238, 242);
    p.menu_text = wxColour(31, 35, 40);
    p.title_bar_background = wxColour(246, 248, 250);
    p.title_bar_text = wxColour(31, 35, 40);
    p.title_bar_border = wxColour(208, 215, 222);
    p.legend_combo_background = wxColour(246, 248, 250);
    p.legend_combo_background_hovered = wxColour(234, 238, 242);
    p.icon_enabled = wxColour(31, 35, 40);
    p.icon_disabled = wxColour(140, 149, 159);
    p.error = wxColour(207, 34, 46);   // GitHub danger #CF222E
    p.warning = wxColour(154, 103, 0); // GitHub attention #9A6700

    p.ruler_background = {0.847f, 0.871f, 0.894f, 0.6f};
    p.legend_window_background = {0.965f, 0.973f, 0.980f, 0.9f};
    p.slider_groove = {0.816f, 0.843f, 0.871f, 0.95f};
    p.slider_border = {0.122f, 0.137f, 0.157f, 1.0f};
    p.ruler_tick = {0.122f, 0.137f, 0.157f, 1.0f};
    p.slider_label_background = {0.918f, 0.933f, 0.949f, 1.0f};
    p.legend_text = {0.122f, 0.137f, 0.157f, 1.0f};
    p.gcode_comment = {0.396f, 0.427f, 0.463f, 1.0f};
    p.imgui_window_bg = {1.0f, 1.0f, 1.0f, 0.95f};
    p.imgui_frame_bg = {0.965f, 0.973f, 0.980f, 1.0f};
    p.imgui_frame_hover = {0.918f, 0.933f, 0.949f, 1.0f};
    p.imgui_text = {0.122f, 0.137f, 0.157f, 1.0f};
    p.imgui_text_disabled = {0.396f, 0.427f, 0.463f, 1.0f};
    p.imgui_border = {0.816f, 0.843f, 0.871f, 1.0f};
    p.imgui_header = {0.918f, 0.627f, 0.196f, 0.3f};
    p.imgui_header_hover = {0.918f, 0.627f, 0.196f, 0.5f};
    p.toolbar_background = {0.918f, 0.933f, 0.949f, 0.92f};

    return p;
}

// Overlays the colors from a theme JSON file onto an existing palette.
// Returns true if the file parsed; missing/invalid keys retain their prior value.
static bool load_theme_file(const fs::path &file, ThemePalette &p)
{
    try
    {
        boost::nowide::ifstream ifs(file.string());
        json j;
        ifs >> j;

        p.name = j.value("name", p.name);
        p.is_dark = j.value("is_dark", p.is_dark);

        if (j.contains("colors") && j["colors"].is_object())
        {
            const json &colors = j["colors"];
            auto set_color = [&colors](const char *key, wxColour &dst)
            {
                if (colors.contains(key) && colors[key].is_string())
                {
                    wxColour c;
                    if (c.Set(wxString::FromUTF8(colors[key].get<std::string>().c_str())))
                        dst = c;
                }
            };

            set_color("input_background", p.input_background);
            set_color("input_background_disabled", p.input_background_disabled);
            set_color("input_foreground", p.input_foreground);
            set_color("input_foreground_disabled", p.input_foreground_disabled);
            set_color("panel_background", p.panel_background);
            set_color("panel_foreground", p.panel_foreground);
            set_color("content_background", p.content_background);
            set_color("content_foreground", p.content_foreground);
            set_color("secondary_text", p.secondary_text);
            set_color("label_default", p.label_default);
            set_color("highlight_label", p.highlight_label);
            set_color("highlight_background", p.highlight_background);
            set_color("hovered_btn_label", p.hovered_btn_label);
            set_color("default_btn_label", p.default_btn_label);
            set_color("selected_btn_background", p.selected_btn_background);
            set_color("header_background", p.header_background);
            set_color("header_hover", p.header_hover);
            set_color("section_header_background", p.section_header_background);
            set_color("section_header_hover", p.section_header_hover);
            set_color("header_divider", p.header_divider);
            set_color("tab_background_normal", p.tab_background_normal);
            set_color("tab_background_hover", p.tab_background_hover);
            set_color("tab_background_selected", p.tab_background_selected);
            set_color("tab_background_disabled", p.tab_background_disabled);
            set_color("tab_text_normal", p.tab_text_normal);
            set_color("tab_text_selected", p.tab_text_selected);
            set_color("tab_text_disabled", p.tab_text_disabled);
            set_color("tab_border", p.tab_border);
            set_color("static_box_border", p.static_box_border);
            set_color("section_border", p.section_border);
            set_color("canvas_background", p.canvas_background);
            set_color("canvas_gradient_top", p.canvas_gradient_top);
            set_color("bed_surface", p.bed_surface);
            set_color("bed_grid", p.bed_grid);
            set_color("menu_background", p.menu_background);
            set_color("menu_hover", p.menu_hover);
            set_color("menu_text", p.menu_text);
            set_color("title_bar_background", p.title_bar_background);
            set_color("title_bar_text", p.title_bar_text);
            set_color("title_bar_border", p.title_bar_border);
            set_color("legend_combo_background", p.legend_combo_background);
            set_color("legend_combo_background_hovered", p.legend_combo_background_hovered);
            set_color("icon_enabled", p.icon_enabled);
            set_color("icon_disabled", p.icon_disabled);
            set_color("accent_primary", p.accent_primary);
            set_color("accent_dark", p.accent_dark);
            set_color("accent_hover", p.accent_hover);
            set_color("accent_secondary", p.accent_secondary);
            set_color("accent_text", p.accent_text);
            set_color("error", p.error);
            set_color("warning", p.warning);
        }

        if (j.contains("imgui") && j["imgui"].is_object())
        {
            const json &im = j["imgui"];
            auto set_rgba = [&im](const char *key, RGBAf &dst)
            {
                if (im.contains(key) && im[key].is_array() && im[key].size() == 4)
                {
                    dst.r = im[key][0].get<float>();
                    dst.g = im[key][1].get<float>();
                    dst.b = im[key][2].get<float>();
                    dst.a = im[key][3].get<float>();
                }
            };

            set_rgba("ruler_background", p.ruler_background);
            set_rgba("legend_window_background", p.legend_window_background);
            set_rgba("slider_groove", p.slider_groove);
            set_rgba("slider_border", p.slider_border);
            set_rgba("ruler_tick", p.ruler_tick);
            set_rgba("slider_label_background", p.slider_label_background);
            set_rgba("legend_text", p.legend_text);
            set_rgba("gcode_comment", p.gcode_comment);
            set_rgba("gcode_command", p.gcode_command);
            set_rgba("imgui_window_bg", p.imgui_window_bg);
            set_rgba("imgui_frame_bg", p.imgui_frame_bg);
            set_rgba("imgui_frame_hover", p.imgui_frame_hover);
            set_rgba("imgui_text", p.imgui_text);
            set_rgba("imgui_text_disabled", p.imgui_text_disabled);
            set_rgba("imgui_border", p.imgui_border);
            set_rgba("imgui_header", p.imgui_header);
            set_rgba("imgui_header_hover", p.imgui_header_hover);
            set_rgba("toolbar_background", p.toolbar_background);
        }
        return true;
    }
    catch (const std::exception &e)
    {
        BOOST_LOG_TRIVIAL(error) << "Theme: failed to parse " << file.string() << ": " << e.what();
        return false;
    }
}

// Reads just the metadata (name/is_dark/auto_default) from theme files in a directory.
// Themes whose name is already present are skipped (so the caller can let one
// directory shadow another by scanning it first).
static void scan_theme_dir(const fs::path &dir, std::vector<ThemeInfo> &out)
{
    if (dir.empty() || !fs::exists(dir) || !fs::is_directory(dir))
        return;

    for (fs::directory_iterator it(dir), end; it != end; ++it)
    {
        if (!fs::is_regular_file(it->status()) || it->path().extension() != ".json")
            continue;

        try
        {
            boost::nowide::ifstream ifs(it->path().string());
            json j;
            ifs >> j;

            ThemeInfo info;
            info.name = j.value("name", it->path().stem().string());
            info.is_dark = j.value("is_dark", true);
            info.auto_default = j.value("auto_default", std::string());
            info.file_path = it->path().string();

            // Skip if a theme with this name was already collected (shadowing).
            bool exists = false;
            for (const ThemeInfo &t : out)
                if (t.name == info.name)
                {
                    exists = true;
                    break;
                }
            if (!exists)
                out.push_back(std::move(info));
        }
        catch (const std::exception &e)
        {
            BOOST_LOG_TRIVIAL(error) << "Theme: skipping unreadable theme file " << it->path().string() << ": "
                                     << e.what();
        }
    }
}

std::vector<ThemeInfo> available_themes()
{
    // On-disk theme files only (the two "Default" themes are compiled in, not files). data_dir/themes
    // is scanned first because scan_theme_dir keeps the first occurrence of a name, so a user theme of
    // the same name shadows a bundled one; the list is then sorted alphabetically for the dropdown.
    std::vector<ThemeInfo> themes;
    scan_theme_dir(fs::path(data_dir()) / "themes", themes);
    scan_theme_dir(fs::path(resources_dir()) / "themes", themes);
    std::sort(themes.begin(), themes.end(), [](const ThemeInfo &a, const ThemeInfo &b) { return a.name < b.name; });
    return themes;
}

// Determines the current theme selection key (app_config "theme"), with legacy-name mapping and a
// migration from the old dark_color_mode flag. "Auto" and the two compiled "Default" themes are
// always valid; a file-theme name that no longer exists on disk falls back to "Auto" so the
// Preferences dropdown and the theme actually loaded stay in sync.
std::string current_theme_selection()
{
    AppConfig *cfg = get_app_config();
    if (!cfg)
        return auto_theme_key();

    std::string sel = cfg->get("theme");

    // Map names from earlier builds (when the defaults were JSON files) to the compiled defaults.
    if (sel == "preFlight Dark")
        sel = default_dark_key();
    else if (sel == "preFlight Light")
        sel = default_light_key();

    if (sel.empty())
    {
        // Migration from the legacy dark_color_mode flag on first run after upgrade.
        if (cfg->has("dark_color_mode"))
            sel = cfg->get_bool("dark_color_mode") ? default_dark_key() : default_light_key();
        else
            sel = auto_theme_key();
    }

    // Always-valid built-in selections.
    if (sel == auto_theme_key() || sel == default_dark_key() || sel == default_light_key())
        return sel;

    // Otherwise it must name a theme file on disk.
    for (const ThemeInfo &t : available_themes())
        if (t.name == sel)
            return sel;
    return auto_theme_key();
}

void load_active_theme()
{
    ThemePalette &p = mutable_palette();
    const std::string selection = current_theme_selection();

    // Persist the resolved selection once so the legacy-flag migration does not re-run every launch.
    if (AppConfig *cfg = get_app_config(); cfg != nullptr && !cfg->has("theme"))
        cfg->set("theme", selection);

    // "Auto" resolves to one of the two compiled defaults based on the OS appearance.
    std::string resolved = selection;
    if (resolved == auto_theme_key())
        resolved = check_dark_mode() ? default_dark_key() : default_light_key();

    // Compiled-in defaults: always available, no file required (the floor that can never be deleted).
    if (resolved == default_dark_key())
    {
        p = ThemePalette{};
        mutable_is_dark() = p.is_dark;
        BOOST_LOG_TRIVIAL(info) << "Theme: compiled Default Dark (selection '" << selection << "')";
        return;
    }
    if (resolved == default_light_key())
    {
        p = default_light_palette();
        mutable_is_dark() = p.is_dark;
        BOOST_LOG_TRIVIAL(info) << "Theme: compiled Default Light (selection '" << selection << "')";
        return;
    }

    // Otherwise it's a theme file on disk. Base on the compiled dark defaults, then overlay the file
    // (so any keys the file omits keep sane values). NOTE: bind available_themes() to a named local;
    // pointing into the returned temporary would dangle once this loop ends.
    p = ThemePalette{};
    const std::vector<ThemeInfo> themes = available_themes();
    const ThemeInfo *chosen = nullptr;
    for (const ThemeInfo &t : themes)
        if (t.name == resolved)
        {
            chosen = &t;
            break;
        }
    if (chosen != nullptr && load_theme_file(chosen->file_path, p))
    {
        mutable_is_dark() = p.is_dark;
        BOOST_LOG_TRIVIAL(info) << "Theme: loaded '" << p.name << "' from " << chosen->file_path;
    }
    else
    {
        // The selected theme file is missing or invalid. Fall back to "Auto" (the OS appearance) rather
        // than forcing Default Dark, and record an error so the GUI can surface a breadcrumb once it is up.
        // The user's selection is intentionally left untouched in app_config, so fixing the file restores it.
        const bool os_dark = check_dark_mode();
        p = os_dark ? ThemePalette{} : default_light_palette();
        mutable_is_dark() = p.is_dark;
        mutable_theme_load_error() = "Theme \"" + resolved + "\" could not be loaded and was not applied. Using Auto.";
        BOOST_LOG_TRIVIAL(error) << "Theme: '" << resolved << "' invalid/unavailable; reverted to Auto ("
                                 << (os_dark ? "Default Dark" : "Default Light") << ")";
    }
}

} // namespace DSKY
