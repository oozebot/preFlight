///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2018 - 2023 Oleksandra Iushchenko @YuSanka, David Kocík @kocikdav, Vojtěch Bubník @bubnikv, Pavel Mikuš @Godrak, Enrico Turri @enricoturri1966, Lukáš Matěna @lukasmatena, Vojtěch Král @vojtechkral
///|/
///|/ ported from lib/Slic3r/GUI/Preferences.pm:
///|/ Copyright (c) Prusa Research 2016 - 2018 Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2013 - 2014 Alessandro Ranellucci @alranel
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "Preferences.hpp"
#include "OptionsGroup.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "ThemePalette.hpp"
#include "Plater.hpp"
#include "MsgDialog.hpp"
#include "I18N.hpp"
#include "format.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "Tab.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/CpuAffinity.hpp"
#include "libslic3r/NvidiaProfile.hpp"
#include <wx/notebook.h>
#include "Notebook.hpp"
#include "ButtonsDescription.hpp"
#include "OG_CustomCtrl.hpp"
#include "GLCanvas3D.hpp"
#include "ConfigWizard.hpp"
#include "Search.hpp"

#include "Widgets/ScrollablePanel.hpp"
#include "Widgets/SpinInput.hpp"
#include "Widgets/UIColors.hpp"

#include <sstream>
#include <cstdlib>
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/nowide/convert.hpp>
#ifdef _WIN32
#include <shellapi.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifdef WIN32
#include <wx/msw/registry.h>
#define _MSW_DARK_MODE
#include "DarkMode.hpp"
#endif // WIN32
#if defined(__linux__) && defined(SLIC3R_DESKTOP_INTEGRATION)
#include "DesktopIntegrationDialog.hpp"
#endif //(__linux__) && defined(SLIC3R_DESKTOP_INTEGRATION)

namespace Slic3r
{

static t_config_enum_names enum_names_from_keys_map(const t_config_enum_values &enum_keys_map)
{
    t_config_enum_names names;
    int cnt = 0;
    for (const auto &kvp : enum_keys_map)
        cnt = std::max(cnt, kvp.second);
    cnt += 1;
    names.assign(cnt, "");
    for (const auto &kvp : enum_keys_map)
        names[kvp.second] = kvp.first;
    return names;
}

#define CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(NAME)                                               \
    static t_config_enum_names s_keys_names_##NAME = enum_names_from_keys_map(s_keys_map_##NAME); \
    template<>                                                                                    \
    const t_config_enum_values &ConfigOptionEnum<NAME>::get_enum_values()                         \
    {                                                                                             \
        return s_keys_map_##NAME;                                                                 \
    }                                                                                             \
    template<>                                                                                    \
    const t_config_enum_names &ConfigOptionEnum<NAME>::get_enum_names()                           \
    {                                                                                             \
        return s_keys_names_##NAME;                                                               \
    }

static const t_config_enum_values s_keys_map_NotifyReleaseMode = {
    {"all", NotifyReleaseAll},
    {"release", NotifyReleaseOnly},
    {"none", NotifyReleaseNone},
};

CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(NotifyReleaseMode)

static const t_config_enum_values s_keys_map_CpuMaxThreadsMode = {
    {"0", CpuMaxThreadsAuto}, {"1", CpuMaxThreadsT1},   {"2", CpuMaxThreadsT2},   {"4", CpuMaxThreadsT4},
    {"6", CpuMaxThreadsT6},   {"8", CpuMaxThreadsT8},   {"12", CpuMaxThreadsT12}, {"16", CpuMaxThreadsT16},
    {"24", CpuMaxThreadsT24}, {"32", CpuMaxThreadsT32},
};

CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(CpuMaxThreadsMode)

static const t_config_enum_values s_keys_map_CanvasLightingQuality = {
    {"auto", CanvasLightingAuto},
    {"basic", CanvasLightingBasic},
    {"enhanced", CanvasLightingEnhanced},
};

CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(CanvasLightingQuality)

static const t_config_enum_values s_keys_map_CanvasMsaaMode = {
    {"auto", CanvasMsaaAuto}, {"0", CanvasMsaaOff}, {"2", CanvasMsaa2x},
    {"4", CanvasMsaa4x},      {"8", CanvasMsaa8x},  {"16", CanvasMsaa16x},
};

CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(CanvasMsaaMode)

static const t_config_enum_values s_keys_map_PreviewDetailLevel = {
    {"1000000", PreviewDetail1M},   {"5000000", PreviewDetail5M}, {"10000000", PreviewDetail10M},
    {"20000000", PreviewDetail20M}, {"0", PreviewDetailFull},
};

CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(PreviewDetailLevel)

namespace GUI
{

PreferencesDialog::PreferencesDialog(wxWindow *parent)
    : DPIDialog(parent, wxID_ANY, _L("Preferences"), wxDefaultPosition, wxDefaultSize,
                wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
#ifdef __WXOSX__
    isOSX = true;
#endif
    build();

    wxSize sz = GetSize();
    bool is_scrollbar_shown = false;

    const size_t pages_cnt = tabs->GetPageCount();
    for (size_t tab_id = 0; tab_id < pages_cnt; tab_id++)
    {
        wxSizer *tab_sizer = tabs->GetPage(tab_id)->GetSizer();
        wxWindow *scrolled = tab_sizer->GetItem(size_t(0))->GetWindow();
        if (auto *sp = dynamic_cast<ScrollablePanel *>(scrolled))
            sp->UpdateScrollbar();
        else if (auto *sw = dynamic_cast<wxScrolledWindow *>(scrolled))
        {
            sw->SetScrollRate(0, 5);
            is_scrollbar_shown |= sw->GetScrollLines(wxVERTICAL) > 0;
        }
    }

    if (is_scrollbar_shown)
        sz.x += 2 * em_unit();
#ifdef __WXGTK__
    // To correct Layout we need at least a small change of size
    else
        sz.x += 1;
#endif
    SetSize(sz);

    m_highlighter.set_timer_owner(this, 0);

#ifdef _WIN32
    // Only apply dark mode if the window handle is valid (prevents GCodeViewer hang)
    if (GetHWND())
        wxGetApp().UpdateDlgDarkUI(this);
#endif
}

static void update_color(wxColourPickerCtrl *color_pckr, const wxColour &color)
{
    if (color_pckr->GetColour() != color)
    {
        color_pckr->SetColour(color);
        wxPostEvent(color_pckr, wxCommandEvent(wxEVT_COLOURPICKER_CHANGED));
    }
}

void PreferencesDialog::show(const std::string &highlight_opt_key /*= std::string()*/,
                             const std::string &tab_name /*= std::string()*/)
{
    int selected_tab = 0;
    for (; selected_tab < int(tabs->GetPageCount()); selected_tab++)
        if (tabs->GetPageText(selected_tab) == _(tab_name))
            break;
    if (selected_tab < int(tabs->GetPageCount()))
        tabs->SetSelection(selected_tab);

    if (!highlight_opt_key.empty())
        init_highlighter(highlight_opt_key);

    // cache input values for custom toolbar size
    m_custom_toolbar_size = atoi(get_app_config()->get("custom_toolbar_size").c_str());
    m_use_custom_toolbar_size = get_app_config()->get_bool("use_custom_toolbar_size");

    // set Field for notify_release to its value
    if (m_optgroup_gui && m_optgroup_gui->get_field("notify_release") != nullptr)
    {
        boost::any val = s_keys_map_NotifyReleaseMode.at(wxGetApp().app_config->get("notify_release"));
        m_optgroup_gui->get_field("notify_release")->set_value(val, false);
    }

    if (wxGetApp().is_editor())
    {
        auto app_config = get_app_config();

        // downloader->set_path_name(app_config->get("url_downloader_dest"));
        // downloader->allow(!app_config->has("downloader_url_registered") || app_config->get_bool("downloader_url_registered"));

        // preFlight: suppress_hyperlinks option is hidden until the hyperlink feature is implemented
        for (const std::string opt_key : {"show_step_import_parameters"})
            m_optgroup_other->set_value(opt_key, app_config->get_bool(opt_key));

        for (const std::string opt_key : {"default_action_on_close_application", "default_action_on_new_project",
                                          "default_action_on_select_preset"})
            m_optgroup_general->set_value(opt_key, app_config->get(opt_key) == "none");
        m_optgroup_general->set_value("default_action_on_dirty_project",
                                      app_config->get("default_action_on_dirty_project").empty());

        // Label colors and mode palette are hardcoded
    }

    // invalidate this flag before show preferences
    m_settings_layout_changed = false;

    this->ShowModal();
}

// ScrollablePanel reports a tiny best size so fixed-area hosts (the settings Tab, the sidebar) stay
// constrained by their parent. The Preferences dialog instead auto-sizes to its content like the old
// native wxScrolledWindow did, so here the panel reports its content height as its best size. The
// dialog can still be shrunk (its own min size allows it), at which point the themed scrollbar shows.
class PreferencesScrollPanel : public ScrollablePanel
{
public:
    using ScrollablePanel::ScrollablePanel;

protected:
    wxSize DoGetBestSize() const override
    {
        wxWindow *c = const_cast<PreferencesScrollPanel *>(this)->GetContentPanel();
        return c ? c->GetBestSize() : ScrollablePanel::DoGetBestSize();
    }
};

static std::shared_ptr<ConfigOptionsGroup> create_options_tab(const wxString &title, wxBookCtrlBase *tabs)
{
    wxPanel *tab = new wxPanel(tabs, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBK_LEFT | wxTAB_TRAVERSAL);

    tabs->AddPage(tab, _(title));
    tab->SetFont(wxGetApp().normal_font());

    // Themed scrollbar host (custom ScrollBar) in place of the native wxScrolledWindow; the option
    // controls live in the panel's content area so they scroll under the themed bar. The subclass
    // reports the content height so the dialog still auto-sizes to fit, as it did before.
    auto scrolled = new PreferencesScrollPanel(tab);
    wxWindow *content = scrolled->GetContentPanel();

#ifdef _WIN32
    wxGetApp().UpdateDarkUI(tab);
    wxGetApp().UpdateDarkUI(content);
#else
    // preFlight: apply theme background on Linux/macOS
    tab->SetBackgroundColour(wxGetApp().get_window_default_clr());
    content->SetBackgroundColour(wxGetApp().get_window_default_clr());
#endif

    // Sizer in the scrolled content area (ScrollablePanel::SetSizer targets the content panel)
    auto *scrolled_sizer = new wxBoxSizer(wxVERTICAL);
    scrolled->SetSizer(scrolled_sizer);

    wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(scrolled, 1, wxEXPAND);
    sizer->SetSizeHints(tab);
    tab->SetSizer(sizer);

    std::shared_ptr<ConfigOptionsGroup> optgroup = std::make_shared<ConfigOptionsGroup>(content);
    optgroup->label_width = 40;
    optgroup->set_config_category_and_type(title, int(Preset::TYPE_PREFERENCES));
    return optgroup;
}

static void activate_options_tab(std::shared_ptr<ConfigOptionsGroup> optgroup)
{
    optgroup->activate([]() {}, wxALIGN_RIGHT);
    optgroup->update_visibility(comSimple);
    wxBoxSizer *sizer = static_cast<wxBoxSizer *>(static_cast<wxPanel *>(optgroup->parent())->GetSizer());
    sizer->Add(optgroup->sizer, 0, wxEXPAND | wxALL, wxGetApp().em_unit());

    optgroup->parent()->Layout();

    // The searcher may not be fully set up in GCodeViewer mode
    if (wxGetApp().is_editor())
    {
        // apply searcher
        wxGetApp().searcher().append_preferences_options(optgroup->get_lines());
    }
}

static void append_bool_option(std::shared_ptr<ConfigOptionsGroup> optgroup, const std::string &opt_key,
                               const std::string &label, const std::string &tooltip, bool def_val,
                               ConfigOptionMode mode = comSimple)
{
    ConfigOptionDef def = {opt_key, coBool};
    def.label = label;
    def.tooltip = tooltip;
    def.mode = mode;
    def.set_default_value(new ConfigOptionBool{def_val});
    Option option(def, opt_key);
    optgroup->append_single_option_line(option);

    // fill data to the Search Dialog
    wxGetApp().searcher().add_key(opt_key, Preset::TYPE_PREFERENCES, optgroup->config_category(), L("Preferences"));
}

template<typename EnumType>
static void append_enum_option(std::shared_ptr<ConfigOptionsGroup> optgroup, const std::string &opt_key,
                               const std::string &label, const std::string &tooltip, const ConfigOption *def_val,
                               std::initializer_list<std::pair<std::string_view, std::string_view>> enum_values,
                               ConfigOptionMode mode = comSimple)
{
    ConfigOptionDef def = {opt_key, coEnum};
    def.label = label;
    def.tooltip = tooltip;
    def.mode = mode;
    def.set_enum<EnumType>(enum_values);

    def.set_default_value(def_val);
    Option option(def, opt_key);
    optgroup->append_single_option_line(option);

    // fill data to the Search Dialog
    wxGetApp().searcher().add_key(opt_key, Preset::TYPE_PREFERENCES, optgroup->config_category(), L("Preferences"));
}

static void append_preferences_option_to_searcher(std::shared_ptr<ConfigOptionsGroup> optgroup,
                                                  const std::string &opt_key, const wxString &label)
{
    Search::OptionsSearcher &searcher = wxGetApp().searcher();
    // fill data to the Search Dialog
    searcher.add_key(opt_key, Preset::TYPE_PREFERENCES, optgroup->config_category(), L("Preferences"));
    // apply sercher
    searcher.append_preferences_option(Line(opt_key, label, ""));
}

void PreferencesDialog::build()
{
#ifdef _WIN32
    // During GCodeViewer mode, the parent MainFrame may not be fully realized when
    // this dialog is constructed, causing GetClientRect to fail on invalid HWND
    if (GetHWND())
        wxGetApp().UpdateDarkUI(this);
#else
    // preFlight: apply theme background on Linux/macOS (UpdateDarkUI is Windows-only)
    SetBackgroundColour(wxGetApp().get_window_default_clr());
#endif
    const wxFont &font = wxGetApp().normal_font();
    SetFont(font);

    auto app_config = get_app_config();

    // preFlight: the owner-drawn Notebook paints its own tab strip from the theme palette, so the
    // tabs follow the active theme on every platform (native wxNotebook tabs ignore our colours).
    tabs = new Notebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                        wxNB_TOP | wxTAB_TRAVERSAL | wxNB_NOPAGETHEME | wxNB_DEFAULT);
#ifdef _MSW_DARK_MODE
    // Only apply if window handle is valid to prevent GCodeViewer hang
    if (tabs->GetHWND())
        wxGetApp().UpdateDarkUI(tabs);
#else
    tabs->SetBackgroundColour(wxGetApp().get_window_default_clr());
#ifdef __linux__
    tabs->Bind(wxEVT_BOOKCTRL_PAGE_CHANGED,
               [this](wxBookCtrlEvent &e)
               {
                   e.Skip();
                   CallAfter(
                       [this]()
                       {
                           if (wxWindow *page = tabs->GetCurrentPage())
                               page->Layout();
                       });
               });
#endif
#endif

    // Add "General" tab
    m_optgroup_general = create_options_tab(L("General"), tabs);
    m_optgroup_general->on_change = [this](t_config_option_key opt_key, boost::any value)
    {
        if (auto it = m_values.find(opt_key); it != m_values.end())
        {
            m_values.erase(
                it); // we shouldn't change value, if some of those parameters were selected, and then deselected
            return;
        }
        if (opt_key == "default_action_on_close_application" || opt_key == "default_action_on_select_preset" ||
            opt_key == "default_action_on_new_project")
            m_values[opt_key] = boost::any_cast<bool>(value) ? "none" : "discard";
        else if (opt_key == "default_action_on_dirty_project")
            m_values[opt_key] = boost::any_cast<bool>(value) ? "" : "0";
        else
            m_values[opt_key] = boost::any_cast<bool>(value) ? "1" : "0";
    };

    bool is_editor = wxGetApp().is_editor();

    if (is_editor)
    {
        append_bool_option(
            m_optgroup_general, "remember_output_path", L("Remember output directory"),
            L("If this is enabled, Slic3r will prompt the last output directory instead of the one containing the input files."),
            app_config->has("remember_output_path") ? app_config->get_bool("remember_output_path") : true);

        append_bool_option(m_optgroup_general, "background_processing", L("Background processing"),
                           L("If this is enabled, preFlight will pre-process objects as soon "
                             "as they're loaded in order to save time when exporting G-code."),
                           app_config->get_bool("background_processing"));

        append_bool_option(
            m_optgroup_general, "alert_when_supports_needed", L("Alert when supports needed"),
            L("If this is enabled, Slic3r will raise alerts when it detects "
              "issues in the sliced object, that can be resolved with supports (and brim). "
              "Examples of such issues are floating object parts, unsupported extrusions and low bed adhesion."),
            app_config->get_bool("alert_when_supports_needed"));

        m_optgroup_general->append_separator();

        // Please keep in sync with ConfigWizard
        append_bool_option(
            m_optgroup_general, "export_sources_full_pathnames", L("Export sources full pathnames to 3mf and amf"),
            L("If enabled, allows the Reload from disk command to automatically find and load the files when invoked."),
            app_config->get_bool("export_sources_full_pathnames"));

#ifdef _WIN32
        // Please keep in sync with ConfigWizard
        append_bool_option(m_optgroup_general, "associate_3mf", L("Associate .3mf files to preFlight"),
                           L("If enabled, sets preFlight as default application to open .3mf files."),
                           app_config->get_bool("associate_3mf"));

        append_bool_option(m_optgroup_general, "associate_stl", L("Associate .stl files to preFlight"),
                           L("If enabled, sets preFlight as default application to open .stl files."),
                           app_config->get_bool("associate_stl"));
#endif // _WIN32

        m_optgroup_general->append_separator();

        // Please keep in sync with ConfigWizard
        append_bool_option(
            m_optgroup_general, "preset_update", L("Update built-in Presets automatically"),
            L("If enabled, Slic3r downloads updates of built-in system presets in the background. These updates are downloaded "
              "into a separate temporary location. When a new preset version becomes available it is offered at application startup."),
            app_config->get_bool("preset_update"));

        append_bool_option(
            m_optgroup_general, "no_defaults", L("Suppress \" - default - \" presets"),
            L("Suppress \" - default - \" presets in the Print / Filament / Printer selections once there are any other valid presets available."),
            app_config->get_bool("no_defaults"));

        append_bool_option(m_optgroup_general, "show_incompatible_presets", L("Show incompatible print presets"),
                           L("When checked, the print presets are shown in the preset editor "
                             "even if they are marked as incompatible with the active printer"),
                           app_config->get_bool("show_incompatible_presets"));

        m_optgroup_general->append_separator();

        append_bool_option(
            m_optgroup_general, "show_drop_project_dialog", L("Show load project dialog"),
            L("When checked, whenever dragging and dropping a project file on the application or open it from a browser, "
              "shows a dialog asking to select the action to take on the file to load."),
            app_config->get_bool("show_drop_project_dialog"));

        append_bool_option(
            m_optgroup_general, "single_instance",
#if __APPLE__
            L("Allow just a single preFlight instance"),
            L("On OSX there is always only one instance of app running by default. However it is allowed to run multiple instances "
              "of same app from the command line. In such case this settings will allow only one instance."),
#else
            L("Allow just a single preFlight instance"),
            L("If this is enabled, when starting preFlight and another instance of the same preFlight is already running, that instance will be reactivated instead."),
#endif
            app_config->has("single_instance") ? app_config->get_bool("single_instance") : false);

        m_optgroup_general->append_separator();

        append_bool_option(m_optgroup_general, "default_action_on_dirty_project",
                           L("Ask for unsaved changes in project"),
                           L("Always ask for unsaved changes in project, when: \n"
                             "- Closing preFlight,\n"
                             "- Loading or creating a new project"),
                           app_config->get("default_action_on_dirty_project").empty());

        m_optgroup_general->append_separator();

        append_bool_option(
            m_optgroup_general, "default_action_on_close_application",
            L("Ask to save unsaved changes in presets when closing the application or when loading a new project"),
            L("Always ask for unsaved changes in presets, when: \n"
              "- Closing preFlight while some presets are modified,\n"
              "- Loading a new project while some presets are modified"),
            app_config->get("default_action_on_close_application") == "none");

        append_bool_option(
            m_optgroup_general, "default_action_on_select_preset",
            L("Ask for unsaved changes in presets when selecting new preset"),
            L("Always ask for unsaved changes in presets when selecting new preset or resetting a preset"),
            app_config->get("default_action_on_select_preset") == "none");

        append_bool_option(m_optgroup_general, "default_action_on_new_project",
                           L("Ask for unsaved changes in presets when creating new project"),
                           L("Always ask for unsaved changes in presets when creating new project"),
                           app_config->get("default_action_on_new_project") == "none");
    }
#ifdef _WIN32
    else
    {
        append_bool_option(m_optgroup_general, "associate_gcode",
                           L("Associate .gcode files to preFlight G-code Viewer"),
                           L("If enabled, sets preFlight G-code Viewer as default application to open .gcode files."),
                           app_config->get_bool("associate_gcode"));
        append_bool_option(m_optgroup_general, "associate_bgcode",
                           L("Associate .bgcode files to preFlight G-code Viewer"),
                           L("If enabled, sets preFlight G-code Viewer as default application to open .bgcode files."),
                           app_config->get_bool("associate_bgcode"));
    }
#endif // _WIN32

#if __APPLE__
    append_bool_option(m_optgroup_general, "use_retina_opengl", L("Use Retina resolution for the 3D scene"),
                       L("If enabled, the 3D scene will be rendered in Retina resolution. "
                         "If you are experiencing 3D performance problems, disabling this option may help."),
                       app_config->get_bool("use_retina_opengl"));
#endif

    m_optgroup_general->append_separator();

    // Show splash screen hardcoded to true

    append_bool_option(m_optgroup_general, "restore_win_position", L("Restore window position on start"),
                       L("If enabled, preFlight will be open at the position it was closed"),
                       app_config->get_bool("restore_win_position"));

    // Clear Undo / Redo stack on new project
    append_bool_option(m_optgroup_general, "clear_undo_redo_stack_on_new_project",
                       L("Clear Undo / Redo stack on new project"),
                       L("Clear Undo / Redo stack on new project or when an existing project is loaded."),
                       app_config->get_bool("clear_undo_redo_stack_on_new_project"));

#if defined(_WIN32) || defined(__APPLE__)
    append_bool_option(m_optgroup_general, "use_legacy_3DConnexion", L("Enable support for legacy 3DConnexion devices"),
                       L("If enabled, the legacy 3DConnexion devices settings dialog is available by pressing CTRL+M"),
                       app_config->get_bool("use_legacy_3DConnexion"));
#endif // _WIN32 || __APPLE__

    activate_options_tab(m_optgroup_general);

    // Add "Camera" tab
    m_optgroup_camera = create_options_tab(L("Camera"), tabs);
    m_optgroup_camera->on_change = [this](t_config_option_key opt_key, boost::any value)
    {
        if (auto it = m_values.find(opt_key); it != m_values.end())
        {
            m_values.erase(
                it); // we shouldn't change value, if some of those parameters were selected, and then deselected
            return;
        }
        m_values[opt_key] = boost::any_cast<bool>(value) ? "1" : "0";
    };

    append_bool_option(m_optgroup_camera, "use_perspective_camera", L("Use perspective camera"),
                       L("If enabled, use perspective camera. If not enabled, use orthographic camera."),
                       app_config->get_bool("use_perspective_camera"));

    append_bool_option(m_optgroup_camera, "use_free_camera", L("Use free camera"),
                       L("If enabled, use free camera. If not enabled, use constrained camera."),
                       app_config->get_bool("use_free_camera"));

    append_bool_option(m_optgroup_camera, "reverse_mouse_wheel_zoom", L("Reverse direction of zoom with mouse wheel"),
                       L("If enabled, reverses the direction of zoom with mouse wheel"),
                       app_config->get_bool("reverse_mouse_wheel_zoom"));

    activate_options_tab(m_optgroup_camera);

    // Add "GUI" tab
    m_optgroup_gui = create_options_tab(L("GUI"), tabs);
    m_optgroup_gui->on_change = [this](t_config_option_key opt_key, boost::any value)
    {
        if (opt_key == "notify_release")
        {
            int val_int = boost::any_cast<int>(value);
            for (const auto &item : s_keys_map_NotifyReleaseMode)
            {
                if (item.second == val_int)
                {
                    m_values[opt_key] = item.first;
                    return;
                }
            }
        }
        if (opt_key == "theme")
        {
            // The theme selection is captured deterministically in accept() (the combo's on_change
            // is not a reliable source here). Return so it doesn't fall through to the bool cast below.
            return;
        }
        if (opt_key == "use_custom_toolbar_size")
        {
            m_icon_size_sizer->ShowItems(boost::any_cast<bool>(value));
            refresh_og(m_optgroup_gui);
            get_app_config()->set("use_custom_toolbar_size", boost::any_cast<bool>(value) ? "1" : "0");
            wxGetApp().plater()->get_current_canvas3D()->render();
            return;
        }

        if (auto it = m_values.find(opt_key); it != m_values.end())
        {
            m_values.erase(
                it); // we shouldn't change value, if some of those parameters were selected, and then deselected
            return;
        }

        /*		if (opt_key == "suppress_hyperlinks")
			m_values[opt_key] = boost::any_cast<bool>(value) ? "1" : "";
		else*/
        m_values[opt_key] = boost::any_cast<bool>(value) ? "1" : "0";
    };

    append_bool_option(
        m_optgroup_gui, "legacy_prepare_layout", L("Legacy layout for Prepare"),
        L("If enabled, the sidebar moves to the right and the gizmo toolbar moves to the left in the Prepare view. "
          "This matches the traditional slicer layout. Requires application restart."),
        app_config->get_bool("legacy_prepare_layout"));

    if (is_editor)
    {
        // show_collapse_button and color_mapinulation_panel hardcoded to false

        append_bool_option(
            m_optgroup_gui, "order_volumes", L("Order object volumes by types"),
            L("If enabled, volumes will be always ordered inside the object. Correct order is Model Part, Negative Volume, Modifier, Support Blocker and Support Enforcer. "
              "If disabled, you can reorder Model Parts, Negative Volumes and Modifiers. But one of the model parts have to be on the first place."),
            app_config->get_bool("order_volumes"));

        append_bool_option(m_optgroup_gui, "non_manifold_edges", L("Show non-manifold edges"),
                           L("If enabled, shows non-manifold edges."), app_config->get_bool("non_manifold_edges"));

        append_bool_option(
            m_optgroup_gui, "allow_auto_color_change", L("Allow automatically color change"),
            L("If enabled, related notification will be shown, when sliced object looks like a logo or a sign."),
            app_config->get_bool("allow_auto_color_change"));

        m_optgroup_gui->append_separator();
        /*
		append_bool_option(m_optgroup_gui, "suppress_round_corners",
			L("Suppress round corners for controls (experimental)"),
			L("If enabled, Settings Tabs will be placed as menu items. If disabled, old UI will be used."),
			app_config->get("suppress_round_corners") == "1");

		m_optgroup_gui->append_separator();
*/
        // append_bool_option(m_optgroup_gui, "show_hints",
        // 	L("Show \"Tip of the day\" notification after start"),
        // 	L("If enabled, useful hints are displayed at startup."),
        // 	app_config->get_bool("show_hints"));

        // preFlight: Theme selector. Entries are the theme files on disk; "Auto" follows the
        // OS light/dark setting. Changing the theme restarts preFlight (options_to_recreate_GUI).
        {
            ConfigOptionDef theme_def;
            theme_def.label = L("Theme");
            theme_def.type = coStrings;
            theme_def.tooltip = L("Select the application color theme. \"Auto\" follows your operating "
                                  "system's light/dark setting. Changing the theme restarts preFlight.");
            // Auto, the two compiled-in defaults, then every theme file on disk (alpha-sorted).
            std::vector<std::string> theme_entries;
            theme_entries.emplace_back(auto_theme_key());    // "Auto"
            theme_entries.emplace_back(default_light_key()); // "Default Light"
            theme_entries.emplace_back(default_dark_key());  // "Default Dark"
            for (const ThemeInfo &t : available_themes())
                theme_entries.push_back(t.name);
            // Values only (no labels); the Choice field falls back to displaying the values.
            theme_def.set_enum_values(theme_entries, std::initializer_list<std::string_view>{});
            theme_def.gui_type = ConfigOptionDef::GUIType::select_close;
            theme_def.set_default_value(new ConfigOptionStrings{current_theme_selection()});
            Option theme_opt(theme_def, "theme");
            m_optgroup_gui->append_single_option_line(theme_opt);
            wxGetApp().searcher().add_key("theme", Preset::TYPE_PREFERENCES, m_optgroup_gui->config_category(),
                                          L("Preferences"));
        }

        append_enum_option<NotifyReleaseMode>(
            m_optgroup_gui, "notify_release", L("Notify about new releases"),
            L("You will be notified about new release after startup acordingly: All = Regular release and alpha / beta releases. Release only = regular release."),
            new ConfigOptionEnum<NotifyReleaseMode>(
                static_cast<NotifyReleaseMode>(s_keys_map_NotifyReleaseMode.at(app_config->get("notify_release")))),
            {{"all", L("All")}, {"release", L("Release only")}, {"none", L("None")}});

        // use_custom_toolbar_size hardcoded to 100%
    }

    activate_options_tab(m_optgroup_gui);

    if (is_editor)
    {
        // set Field for notify_release to its value to activate the object
        boost::any val = s_keys_map_NotifyReleaseMode.at(app_config->get("notify_release"));
        m_optgroup_gui->get_field("notify_release")->set_value(val, false);

        // Removed icon_size_slider, settings_mode_widget, text colors, mode markers
        // These are all hardcoded in AppConfig.cpp and GUI_App.cpp

        m_optgroup_other = create_options_tab(_L("Other"), tabs);
        m_optgroup_other->on_change = [this](t_config_option_key opt_key, boost::any value)
        {
            if (auto it = m_values.find(opt_key); it != m_values.end() && opt_key != "url_downloader_dest")
            {
                m_values.erase(
                    it); // we shouldn't change value, if some of those parameters were selected, and then deselected
                return;
            }

            if (opt_key == "suppress_hyperlinks")
                m_values[opt_key] = boost::any_cast<bool>(value) ? "1" : "";
            else
                m_values[opt_key] = boost::any_cast<bool>(value) ? "1" : "0";
        };

        append_bool_option(m_optgroup_other, "use_binary_gcode_when_supported",
                           L("Use binary G-code when the printer supports it"),
                           L("If the 'Supports binary G-code' option is enabled in Printer Settings, "
                             "checking this option will result in the export of G-code in binary format."),
                           app_config->get_bool("use_binary_gcode_when_supported"));

        // preFlight: hidden until the documentation-hyperlink feature is implemented; suppress_hyperlinks
        // is force-set to "1" in AppConfig so parameter labels never render as links in the meantime.
        // append_bool_option(
        //     m_optgroup_other, "suppress_hyperlinks", L("Suppress to open hyperlink in browser"),
        //     L("If enabled, preFlight will not open a hyperlinks in your browser."),
        //     app_config->get_bool("suppress_hyperlinks"));

        append_bool_option(
            m_optgroup_other, "show_step_import_parameters", L("Show STEP file import parameters"),
            L("If enabled, preFlight will show a dialog with quality selection when importing a STEP file."),
            app_config->get_bool("show_step_import_parameters"));

        // append_bool_option(m_optgroup_other, "show_login_button",
        // 	L("Show \"Log in\" button in application top bar"),
        // 	L("If enabled, preFlight will show up \"Log in\" button in application top bar."),
        // 	app_config->get_bool("show_login_button"));

        // append_bool_option(m_optgroup_other, "downloader_url_registered",
        // 	L("Allow downloads from supported websites (e.g. Printables.com)"),
        // 	L("If enabled, preFlight can download and open files from supported websites"),
        // 	app_config->get_bool("downloader_url_registered"));

        activate_options_tab(m_optgroup_other);

        // create_downloader_path_sizer();
        create_settings_font_widget();

        // CPU tab: thread cap (stability trade) plus P-core preference on hybrid Intel (can also improve
        // slicing speed by avoiding E-cores stalling parallel synchronization points).
        m_optgroup_cpu = create_options_tab(L("Performance"), tabs);
        // Write through directly to AppConfig + live-apply on each change. This matches the pattern
        // used by "use_custom_toolbar_size" in the GUI tab and bypasses the m_values staging map, which
        // for whatever reason was not persisting these two keys in practice.
        m_optgroup_cpu->on_change = [](t_config_option_key opt_key, boost::any value)
        {
            auto *app_config = get_app_config();
            if (opt_key == "canvas_lighting_quality")
            {
                int val_int = boost::any_cast<int>(value);
                static const std::map<int, std::string> lighting_keys = {
                    {CanvasLightingAuto, "auto"},
                    {CanvasLightingBasic, "basic"},
                    {CanvasLightingEnhanced, "enhanced"},
                };
                auto it = lighting_keys.find(val_int);
                if (it != lighting_keys.end())
                {
                    app_config->set(opt_key, it->second);
                    // Request phong shader compilation for the next render pass (needs GL context)
                    if (it->second == "enhanced" || it->second == "auto")
                    {
                        wxGetApp().request_phong_shaders();
                        if (auto *canvas = wxGetApp().plater() ? wxGetApp().plater()->get_current_canvas3D() : nullptr)
                            canvas->request_extra_frame();
                    }
                }
                return;
            }
            if (opt_key == "canvas_msaa")
            {
                int val_int = boost::any_cast<int>(value);
                static const std::map<int, std::string> msaa_keys = {
                    {CanvasMsaaAuto, "auto"}, {CanvasMsaaOff, "0"}, {CanvasMsaa2x, "2"},
                    {CanvasMsaa4x, "4"},      {CanvasMsaa8x, "8"},  {CanvasMsaa16x, "16"},
                };
                auto it = msaa_keys.find(val_int);
                if (it != msaa_keys.end())
                    app_config->set(opt_key, it->second);
                return;
            }
            if (opt_key == "preview_detail")
            {
                int val_int = boost::any_cast<int>(value);
                static const std::map<int, std::string> detail_keys = {
                    {PreviewDetail1M, "1000000"},   {PreviewDetail5M, "5000000"}, {PreviewDetail10M, "10000000"},
                    {PreviewDetail20M, "20000000"}, {PreviewDetailFull, "0"},
                };
                auto it = detail_keys.find(val_int);
                if (it != detail_keys.end())
                    app_config->set(opt_key, it->second);
                return;
            }
            if (opt_key == "cpu_max_slicing_threads")
            {
                int val_int = boost::any_cast<int>(value);
                for (const auto &item : s_keys_map_CpuMaxThreadsMode)
                {
                    if (item.second == val_int)
                    {
                        app_config->set(opt_key, item.first);
                        int max_threads = atoi(item.first.c_str());
                        if (max_threads > 0)
                        {
                            Slic3r::thread_count = static_cast<std::size_t>(max_threads);
                            Slic3r::enforce_thread_count(static_cast<std::size_t>(max_threads));
                        }
                        else
                        {
                            Slic3r::thread_count.reset();
                            Slic3r::enforce_thread_count(0);
                        }
                        return;
                    }
                }
                return;
            }
            if (opt_key == "cpu_pcores_only")
            {
                const bool on = boost::any_cast<bool>(value);
                app_config->set(opt_key, on ? "1" : "0");
                if (on)
                    Slic3r::apply_pcore_only_affinity();
                else
                    Slic3r::restore_full_cpu_affinity();
                return;
            }
            if (opt_key == "cpu_nvidia_disable_threaded_opt")
            {
                const bool on = boost::any_cast<bool>(value);
                app_config->set(opt_key, on ? "1" : "0");
                // Writes the NVIDIA per-app profile setting; takes effect on the next preFlight launch.
                // If the driver refuses or silently ignores the write, users can follow the manual
                // instructions printed directly below the checkbox.
                (void) Slic3r::set_nvidia_threaded_optimization(on);
                return;
            }
        };

        {
            // Resolve the stored AppConfig value to an enum id. If the stored string does not match
            // any dropdown entry (custom CLI value, corrupted config), fall back to Auto.
            const std::string current = app_config->get("cpu_max_slicing_threads");
            auto it = s_keys_map_CpuMaxThreadsMode.find(current);
            CpuMaxThreadsMode current_mode = it != s_keys_map_CpuMaxThreadsMode.end()
                                                 ? static_cast<CpuMaxThreadsMode>(it->second)
                                                 : CpuMaxThreadsAuto;
            append_enum_option<CpuMaxThreadsMode>(m_optgroup_cpu, "cpu_max_slicing_threads",
                                                  L("Maximum slicing threads"),
                                                  L("Limits the number of parallel worker threads used during slicing. "
                                                    "Auto uses all available cores. Lower values trade slicing speed "
                                                    "for stability on CPUs that crash under heavy parallel load."),
                                                  new ConfigOptionEnum<CpuMaxThreadsMode>(current_mode),
                                                  {{"0", L("Auto")},
                                                   {"1", "1"},
                                                   {"2", "2"},
                                                   {"4", "4"},
                                                   {"6", "6"},
                                                   {"8", "8"},
                                                   {"12", "12"},
                                                   {"16", "16"},
                                                   {"24", "24"},
                                                   {"32", "32"}});
        }

#ifdef SLIC3R_CPU_AFFINITY_SUPPORTED
        const bool hybrid = Slic3r::has_hybrid_cpu_topology();
        append_bool_option(m_optgroup_cpu, "cpu_pcores_only",
                           hybrid ? L("Prefer Performance cores")
                                  : L("Prefer Performance cores (not available: no hybrid cores detected)"),
                           L("On Intel hybrid CPUs (12th gen and later), restricts preFlight to the Performance cores "
                             "only. This can improve slicing speed by avoiding the slower Efficient cores at parallel "
                             "synchronization points, and also helps on CPUs that crash under heavy mixed-core load."),
                           app_config->get_bool("cpu_pcores_only"));
#endif // SLIC3R_CPU_AFFINITY_SUPPORTED

        m_optgroup_cpu->append_separator();

        {
            const std::string current = app_config->get("canvas_lighting_quality");
            CanvasLightingQuality current_mode = CanvasLightingAuto;
            if (current == "basic")
                current_mode = CanvasLightingBasic;
            else if (current == "enhanced")
                current_mode = CanvasLightingEnhanced;

            append_enum_option<CanvasLightingQuality>(
                m_optgroup_cpu, "canvas_lighting_quality", L("Lighting quality"),
                L("Controls per-pixel lighting on 3D models. Auto detects your GPU and picks "
                  "the best option. Enhanced uses Blinn-Phong shading with specular highlights "
                  "and rim lighting for a more realistic look. Basic uses flat Gouraud shading."),
                new ConfigOptionEnum<CanvasLightingQuality>(current_mode),
                {{"auto", L("Auto (detect GPU)")}, {"basic", L("Basic")}, {"enhanced", L("Enhanced")}});
        }

        {
            const std::string current = app_config->get("canvas_msaa");
            CanvasMsaaMode current_mode = CanvasMsaaAuto;
            auto it = s_keys_map_CanvasMsaaMode.find(current);
            if (it != s_keys_map_CanvasMsaaMode.end())
                current_mode = static_cast<CanvasMsaaMode>(it->second);

            append_enum_option<CanvasMsaaMode>(
                m_optgroup_cpu, "canvas_msaa", L("Anti-aliasing (MSAA)"),
                L("Controls multi-sample anti-aliasing for smooth edges. Auto tries the highest "
                  "level your GPU supports. Higher values give smoother edges but use more GPU memory. "
                  "Requires application restart to take effect."),
                new ConfigOptionEnum<CanvasMsaaMode>(current_mode),
                {{"auto", L("Auto (detect GPU)")},
                 {"0", L("Off")},
                 {"2", L("2x")},
                 {"4", L("4x")},
                 {"8", L("8x")},
                 {"16", L("16x")}});
        }

        // Preview detail level
        {
            const std::string current_detail = app_config->get("preview_detail");
            PreviewDetailLevel current_level = PreviewDetail10M;
            auto it = s_keys_map_PreviewDetailLevel.find(current_detail);
            if (it != s_keys_map_PreviewDetailLevel.end())
                current_level = static_cast<PreviewDetailLevel>(it->second);

            append_enum_option<PreviewDetailLevel>(
                m_optgroup_cpu, "preview_detail", L("Preview Detail"),
                L("Controls how much detail is computed for the G-code preview on large prints. "
                  "Lower values reduce memory usage and speed up slicing at the cost of less detailed "
                  "preview data. Does not affect print output, G-code, or time estimation."),
                new ConfigOptionEnum<PreviewDetailLevel>(current_level),
                {{"1000000",
#if defined(__linux__) && defined(__aarch64__)
                  L("1M segments (default)")
#else
                  L("1M segments")
#endif
                 },
                 {"5000000", L("5M segments")},
                 {"10000000",
#if defined(__linux__) && defined(__aarch64__)
                  L("10M segments")
#else
                  L("10M segments (default)")
#endif
                 },
                 {"20000000", L("20M segments")},
                 {"0", L("Full (no limit)")}});
        }

        // NVIDIA GPU: optional per-app driver profile fix. Only shown when an NVIDIA driver is
        // actually present on the machine, so AMD/Intel users don't see an irrelevant option.
        const bool has_nvidia = Slic3r::nvidia_driver_available();
        if (has_nvidia)
        {
            m_optgroup_cpu->append_separator();
            append_bool_option(m_optgroup_cpu, "cpu_nvidia_disable_threaded_opt",
                               L("Disable NVIDIA OpenGL Threaded Optimization"),
                               L("Writes a per-application NVIDIA driver profile for preFlight.exe that turns off "
                                 "OpenGL Threaded Optimization. This is the other common cause of slicing crashes "
                                 "on Windows. Takes effect on the next preFlight launch. Unchecking restores the "
                                 "driver default."),
                               false);

            Line nvidia_instructions{"", ""};
            nvidia_instructions.full_width = 1;
            nvidia_instructions.widget = [](wxWindow *parent)
            {
                auto *sizer = new wxBoxSizer(wxHORIZONTAL);
                auto *text = new wxStaticText(
                    parent, wxID_ANY,
                    _L("Only needed if preFlight crashes during slicing. NVIDIA's Threaded Optimization "
                       "can conflict with preFlight's parallel slicing engine.\n\n"
                       "If the checkbox above does not fix the crash, apply it manually:\n"
                       "1. Open NVIDIA Control Panel.\n"
                       "2. Go to 3D Settings \u2192 Manage 3D Settings \u2192 Program Settings.\n"
                       "3. Select preFlight from the program list (click Add and browse to preFlight.exe if "
                       "missing).\n"
                       "4. Set Threaded Optimization to Off and click Apply.\n"
                       "5. Relaunch preFlight."));
                text->Wrap(55 * wxGetApp().em_unit());
                sizer->Add(text, 1, wxEXPAND | wxLEFT | wxRIGHT, wxGetApp().em_unit());
                return sizer;
            };
            m_optgroup_cpu->append_line(nvidia_instructions);
        }

        activate_options_tab(m_optgroup_cpu);

        // Re-enable change events on enum dropdowns. Choice::set_selection() (called during BUILD)
        // sets m_disable_change_event = true and never clears it, so without this call the dropdown's
        // on_change never fires. This mirrors the explicit set_value pattern notify_release uses.
        {
            const std::string current = app_config->get("cpu_max_slicing_threads");
            auto it = s_keys_map_CpuMaxThreadsMode.find(current);
            int val_int = it != s_keys_map_CpuMaxThreadsMode.end() ? it->second : CpuMaxThreadsAuto;
            if (Field *field = m_optgroup_cpu->get_field("cpu_max_slicing_threads"))
                field->set_value(boost::any(val_int), false);
        }
        {
            const std::string current = app_config->get("canvas_lighting_quality");
            int val_int = CanvasLightingAuto;
            if (current == "basic")
                val_int = CanvasLightingBasic;
            else if (current == "enhanced")
                val_int = CanvasLightingEnhanced;
            if (Field *field = m_optgroup_cpu->get_field("canvas_lighting_quality"))
                field->set_value(boost::any(val_int), false);
        }
        {
            const std::string current = app_config->get("canvas_msaa");
            auto it = s_keys_map_CanvasMsaaMode.find(current);
            int val_int = it != s_keys_map_CanvasMsaaMode.end() ? it->second : CanvasMsaaAuto;
            if (Field *field = m_optgroup_cpu->get_field("canvas_msaa"))
                field->set_value(boost::any(val_int), false);
        }

        {
            const std::string current = app_config->get("preview_detail");
            auto it = s_keys_map_PreviewDetailLevel.find(current);
            int val_int = it != s_keys_map_PreviewDetailLevel.end() ? it->second : PreviewDetail10M;
            if (Field *field = m_optgroup_cpu->get_field("preview_detail"))
                field->set_value(boost::any(val_int), false);
        }

        if (has_nvidia)
        {
            if (Field *field = m_optgroup_cpu->get_field("cpu_nvidia_disable_threaded_opt"))
                field->set_value(boost::any(app_config->get_bool("cpu_nvidia_disable_threaded_opt")), false);
        }

#ifdef SLIC3R_CPU_AFFINITY_SUPPORTED
        if (!Slic3r::has_hybrid_cpu_topology())
        {
            if (Field *field = m_optgroup_cpu->get_field("cpu_pcores_only"))
                field->toggle(false);
        }
#endif // SLIC3R_CPU_AFFINITY_SUPPORTED

        // Add "Preprocessing" tab - must match panel > scrolledwindow structure
        // that the constructor's post-build loop expects (GetItem(0) must be wxScrolledWindow)
        {
            wxPanel *pp_tab = new wxPanel(tabs, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                          wxBK_LEFT | wxTAB_TRAVERSAL);
            tabs->AddPage(pp_tab, _L("Preprocessing"));
            pp_tab->SetFont(wxGetApp().normal_font());

            auto *scrolled = new wxScrolledWindow(pp_tab);
#ifdef _WIN32
            wxGetApp().UpdateDarkUI(pp_tab);
            wxGetApp().UpdateDarkUI(scrolled);
#else
            pp_tab->SetBackgroundColour(wxGetApp().get_window_default_clr());
            scrolled->SetBackgroundColour(wxGetApp().get_window_default_clr());
#endif

            auto *content_sizer = new wxBoxSizer(wxVERTICAL);
            int em = wxGetApp().em_unit();

            auto *order_label = new wxStaticText(scrolled, wxID_ANY, _L("Script category execution order:"));
            order_label->SetFont(wxGetApp().normal_font());
#ifndef _WIN32
            order_label->SetForegroundColour(wxGetApp().get_label_clr_default());
#endif

#ifdef _WIN32
            auto *order_listbox = new wxListBox(scrolled, wxID_ANY, wxDefaultPosition, wxSize(-1, 80), 0, nullptr,
                                                wxBORDER_SIMPLE);
            wxGetApp().UpdateDarkUI(order_listbox);
#else
            auto *order_border = new wxPanel(scrolled, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
            wxColour order_border_clr = wxGetApp().get_label_clr_default();
            order_border_clr = wxColour(order_border_clr.Red(), order_border_clr.Green(), order_border_clr.Blue(), 80);
            order_border->SetBackgroundColour(order_border_clr);
            auto *order_border_sizer = new wxBoxSizer(wxVERTICAL);
            auto *order_listbox = new wxListBox(order_border, wxID_ANY, wxDefaultPosition, wxSize(-1, 80), 0, nullptr,
                                                wxBORDER_NONE);
            order_listbox->SetBackgroundColour(wxGetApp().get_window_default_clr());
            order_listbox->SetForegroundColour(wxGetApp().get_label_clr_default());
            order_border_sizer->Add(order_listbox, 1, wxEXPAND | wxALL, 1);
            order_border->SetSizer(order_border_sizer);
#endif
            std::string order_str = app_config->get("preprocessing_category_order");
            if (order_str.empty())
                order_str = "print,filament,printer";
            std::istringstream iss(order_str);
            std::string cat;
            while (std::getline(iss, cat, ','))
            {
                cat.erase(0, cat.find_first_not_of(" \t"));
                cat.erase(cat.find_last_not_of(" \t") + 1);
                if (cat == "print")
                    order_listbox->Append(_L("Print"));
                else if (cat == "filament")
                    order_listbox->Append(_L("Filament"));
                else if (cat == "printer")
                    order_listbox->Append(_L("Printer"));
            }

            auto *order_btn_sizer = new wxBoxSizer(wxHORIZONTAL);
            auto *btn_order_up = new ScalableButton(scrolled, wxID_ANY, "toolbar_arrow", _L("Move Up"), wxDefaultSize,
                                                    wxDefaultPosition, wxBU_LEFT | wxBU_EXACTFIT);
            auto *btn_order_down = new ScalableButton(scrolled, wxID_ANY, "toolbar_arrow_down", _L("Move Down"),
                                                      wxDefaultSize, wxDefaultPosition, wxBU_LEFT | wxBU_EXACTFIT);
            order_btn_sizer->Add(btn_order_up, 0, wxRIGHT, 5);
            order_btn_sizer->Add(btn_order_down, 0);

            auto sync_order = [this, order_listbox]()
            {
                std::string result;
                for (unsigned int i = 0; i < order_listbox->GetCount(); ++i)
                {
                    wxString item = order_listbox->GetString(i);
                    if (!result.empty())
                        result += ",";
                    if (item == _L("Print"))
                        result += "print";
                    else if (item == _L("Filament"))
                        result += "filament";
                    else if (item == _L("Printer"))
                        result += "printer";
                }
                m_values["preprocessing_category_order"] = result;
            };

            btn_order_up->Bind(wxEVT_BUTTON,
                               [order_listbox, sync_order](wxCommandEvent &)
                               {
                                   int sel = order_listbox->GetSelection();
                                   if (sel > 0)
                                   {
                                       wxString item = order_listbox->GetString(sel);
                                       order_listbox->Delete(sel);
                                       order_listbox->Insert(item, sel - 1);
                                       order_listbox->SetSelection(sel - 1);
                                       sync_order();
                                   }
                               });

            btn_order_down->Bind(wxEVT_BUTTON,
                                 [order_listbox, sync_order](wxCommandEvent &)
                                 {
                                     int sel = order_listbox->GetSelection();
                                     if (sel != wxNOT_FOUND && sel < (int) order_listbox->GetCount() - 1)
                                     {
                                         wxString item = order_listbox->GetString(sel);
                                         order_listbox->Delete(sel);
                                         order_listbox->Insert(item, sel + 1);
                                         order_listbox->SetSelection(sel + 1);
                                         sync_order();
                                     }
                                 });

            auto *reset_btn = new wxButton(scrolled, wxID_ANY, _L("Reset Preprocessing Consent"));
            reset_btn->Bind(
                wxEVT_BUTTON,
                [](wxCommandEvent &)
                {
                    MessageDialog confirm(nullptr,
                                          _L("This will revoke your preprocessing consent.\n\n"
                                             "All preprocessing will be disabled across your Print, Filament, "
                                             "and Printer profiles. No Python scripts will run during slicing "
                                             "until you re-enable preprocessing and accept the consent prompt "
                                             "again.\n\n"
                                             "Your saved profiles will not be modified, but preprocessing will "
                                             "remain inactive until you consent again.\n\n"
                                             "Do you want to revoke your preprocessing consent?"),
                                          _L("Revoke Preprocessing Consent"), wxICON_WARNING | wxYES | wxNO);
                    if (confirm.ShowModal() != wxID_YES)
                        return;

                    wxGetApp().app_config->set("preprocessing_consent_accepted", "0");

                    auto *bundle = wxGetApp().preset_bundle;
                    if (bundle)
                    {
                        auto &print_cfg = bundle->prints.get_edited_preset().config;
                        if (print_cfg.has("preprocessing_enabled_print"))
                            print_cfg.set_key_value("preprocessing_enabled_print", new ConfigOptionBool(false));

                        auto &filament_cfg = bundle->filaments.get_edited_preset().config;
                        if (filament_cfg.has("preprocessing_enabled_filament"))
                            filament_cfg.set_key_value("preprocessing_enabled_filament", new ConfigOptionBool(false));

                        auto &printer_cfg = bundle->printers.get_edited_preset().config;
                        if (printer_cfg.has("preprocessing_enabled_printer"))
                            printer_cfg.set_key_value("preprocessing_enabled_printer", new ConfigOptionBool(false));

                        if (auto *tab = wxGetApp().get_tab(Preset::TYPE_PRINT))
                            tab->reload_config();
                        if (auto *tab = wxGetApp().get_tab(Preset::TYPE_FILAMENT))
                            tab->reload_config();
                        if (auto *tab = wxGetApp().get_tab(Preset::TYPE_PRINTER))
                            tab->reload_config();
                    }
                });

            // Python Console section
            auto *console_label = new wxStaticText(scrolled, wxID_ANY, _L("Python packages"));
            console_label->SetFont(wxGetApp().bold_font());
#ifndef _WIN32
            console_label->SetForegroundColour(wxGetApp().get_label_clr_default());
#endif

            auto *console_desc =
                new wxStaticText(scrolled, wxID_ANY,
                                 _L("preFlight includes a built-in Python interpreter for preprocessing scripts. "
                                    "The standard library is available by default. If your scripts require "
                                    "additional packages (e.g. numpy), install them from the Python Console "
                                    "and they become available to all preprocessing scripts:\n\n"
                                    "  1. Click \"Open Python Console\" below\n"
                                    "  2. Install a package:  pip install --only-binary=:all: numpy\n\n"
                                    "Only packages with a prebuilt wheel can be installed (no compiler is bundled)."));
            console_desc->Wrap(42 * em);
#ifndef _WIN32
            console_desc->SetForegroundColour(wxGetApp().get_label_clr_default());
#endif

            auto *btn_console = new wxButton(scrolled, wxID_ANY, _L("Open Python Console"));
#ifdef _WIN32
            wxGetApp().UpdateDarkUI(btn_console);
#endif
            btn_console->Bind(
                wxEVT_BUTTON,
                [](wxCommandEvent &)
                {
                    auto exe_dir = boost::dll::program_location().parent_path();

                    // Packages install to the per-user data dir (survives upgrades; the slicer's
                    // embedded interpreter reads the same dir via user_python_packages_dir()).
                    std::string user_pkg_dir = user_python_packages_dir();
                    {
                        boost::system::error_code ec;
                        boost::filesystem::create_directories(user_pkg_dir, ec);
                    }
#ifdef _WIN32
                    auto python_dir = exe_dir / "python";

                    auto python_exe = (python_dir / "python.exe").string();
                    // Check that bundled Python exists
                    if (!boost::filesystem::exists(python_exe))
                    {
                        MessageDialog(nullptr,
                                      _L("Bundled Python runtime not found.\n\n"
                                         "The python/ directory may be missing from your preFlight installation."),
                                      _L("Python Console"), wxICON_ERROR | wxOK)
                            .ShowModal();
                        return;
                    }
                    // Open a cmd window with the python directory in PATH and Scripts in PATH
                    // so both python and pip (once installed) work directly.
                    std::string cmd_args = "/k \"set PATH=" + python_dir.string() + ";" +
                                           (python_dir / "Scripts").string() +
                                           ";%PATH% && "
                                           "set \"PIP_TARGET=" +
                                           user_pkg_dir +
                                           "\" && "
                                           "set \"PIP_DISABLE_PIP_VERSION_CHECK=1\" && "
                                           "set \"PYTHONDONTWRITEBYTECODE=1\" && "
                                           "doskey pip=python -m pip $* && "
                                           "doskey pip3=python -m pip $* && "
                                           "cd /d " +
                                           exe_dir.string() +
                                           " && "
                                           "title preFlight Python Console && "
                                           "echo. && "
                                           "echo preFlight Python Console && "
                                           "echo ========================= && "
                                           "echo. && "
                                           "echo Packages install to: " +
                                           user_pkg_dir +
                                           " && "
                                           "echo. && "
                                           "echo To install a package: pip install --only-binary=:all: numpy && "
                                           "echo To list packages: pip list --path \"%PIP_TARGET%\" && "
                                           "echo. \"";
                    // Widen to UTF-16 so a non-ASCII data-dir path (e.g. a non-ASCII Windows
                    // username) reaches cmd.exe intact instead of being mangled through the ANSI
                    // code page and sending pip installs to a directory the slicer never reads.
                    std::wstring wcmd = boost::nowide::widen(cmd_args);
                    ShellExecuteW(nullptr, L"open", L"cmd.exe", wcmd.c_str(), nullptr, SW_SHOW);
#else
                    auto python_dir = exe_dir / ".." / "python";
                    auto python_bin = python_dir / "bin";
                    if (!boost::filesystem::exists(python_bin / "python3"))
                    {
                        MessageDialog(nullptr, _L("Bundled Python runtime not found."), _L("Python Console"),
                                      wxICON_ERROR | wxOK)
                            .ShowModal();
                        return;
                    }

                    // Write a temp launch script with a unique name (O_EXCL prevents
                    // symlink attacks, unique_path prevents TOCTOU races).
                    auto script_path = boost::filesystem::temp_directory_path() /
                                       boost::filesystem::unique_path("preflight-console-%%%%-%%%%");
#ifdef __APPLE__
                    // Terminal.app requires .command extension to execute scripts
                    script_path += ".command";
#else
                    script_path += ".sh";
#endif
                    // Use POSIX open with O_EXCL to prevent symlink following
                    int fd = open(script_path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0700);
                    if (fd < 0)
                    {
                        MessageDialog(nullptr, _L("Failed to create temporary console script."), _L("Python Console"),
                                      wxICON_ERROR | wxOK)
                            .ShowModal();
                        return;
                    }
                    // Shell-escape paths: replace ' with '\'' for safe single-quoting
                    auto shell_escape = [](const std::string &s) -> std::string
                    {
                        std::string result = "'";
                        for (char c : s)
                        {
                            if (c == '\'')
                                result += "'\\''";
                            else
                                result += c;
                        }
                        result += "'";
                        return result;
                    };
                    // The source-built python cannot find its own libpython from bin/. On the AppImage
                    // libpython + deps are deduped into ../lib with a bundled loader; on the .deb they
                    // sit in python/lib under the system loader. Wrap python3 to handle both, and route
                    // pip installs to the per-user data dir (PIP_TARGET) so the slicer's interpreter sees them.
                    auto pybin = python_bin / "python3";
                    auto loader = exe_dir / ".." / "lib" / "ld-linux-x86-64.so.2";
                    auto libdir = exe_dir / ".." / "lib";
                    auto pylib = python_dir / "lib";

                    std::string script_content = "#!/bin/bash\n";
                    script_content += "PF_PYBIN=" + shell_escape(pybin.string()) + "\n";
                    script_content += "PF_LOADER=" + shell_escape(loader.string()) + "\n";
                    script_content += "PF_LIBDIR=" + shell_escape(libdir.string()) + "\n";
                    script_content += "PF_PYLIB=" + shell_escape(pylib.string()) + "\n";
                    // Export so the wrapper functions still resolve these after 'exec bash'.
                    script_content += "export PF_PYBIN PF_LOADER PF_LIBDIR PF_PYLIB\n";
                    script_content += "export PIP_TARGET=" + shell_escape(user_pkg_dir) + "\n";
                    script_content += "export PYTHONDONTWRITEBYTECODE=1\n";
                    // Suppress pip's self-upgrade notice: it points users at the bundled python3,
                    // and following it installs a stray pip into PIP_TARGET while never replacing
                    // the running bundled pip, so the notice would repeat forever.
                    script_content += "export PIP_DISABLE_PIP_VERSION_CHECK=1\n";
                    script_content += "if [ -x \"$PF_LOADER\" ]; then\n";
                    script_content += "  python3() { \"$PF_LOADER\" --library-path \"$PF_LIBDIR:$PF_PYLIB\" "
                                      "\"$PF_PYBIN\" \"$@\"; }\n";
                    script_content += "else\n";
                    // No bundled loader (.deb / macOS): system loader + libpython on the path. Set both
                    // LD_LIBRARY_PATH (Linux) and DYLD_LIBRARY_PATH (macOS ignores the former).
                    script_content += "  python3() { LD_LIBRARY_PATH=\"$PF_PYLIB${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}\" "
                                      "DYLD_LIBRARY_PATH=\"$PF_PYLIB${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}\" "
                                      "\"$PF_PYBIN\" \"$@\"; }\n";
                    script_content += "fi\n";
                    script_content += "pip() { python3 -m pip \"$@\"; }\n";
                    script_content += "pip3() { python3 -m pip \"$@\"; }\n";
                    script_content += "export -f python3 pip pip3\n";
                    script_content += "cd " + shell_escape(exe_dir.string()) + "\n";
                    script_content += "echo\n";
                    script_content += "echo 'preFlight Python Console'\n";
                    script_content += "echo '========================='\n";
                    script_content += "echo\n";
                    script_content += "echo \"Packages install to: $PIP_TARGET\"\n";
                    script_content += "echo 'Install a package:  pip install --only-binary=:all: numpy'\n";
                    script_content += "echo 'List packages:      pip list --path \"$PIP_TARGET\"'\n";
                    script_content += "echo\n";
                    script_content += "rm -f " + shell_escape(script_path.string()) + "\n";
                    script_content += "exec bash\n";
                    write(fd, script_content.c_str(), script_content.size());
                    close(fd);

                    // Double-fork to prevent zombie accumulation without globally
                    // altering SIGCHLD (which would break boost::process elsewhere).
                    // The grandchild is reparented to init/systemd and auto-reaped.
                    pid_t pid = fork();
                    if (pid == 0)
                    {
                        pid_t pid2 = fork();
                        if (pid2 == 0)
                        {
#ifdef __APPLE__
                            execlp("open", "open", "-a", "Terminal", script_path.c_str(), nullptr);
#else
                            // Launch the host terminal with a clean library path so bundled libs don't
                            // break it; the console script re-points python at the bundled runtime.
                            unsetenv("LD_LIBRARY_PATH");
                            execlp("gnome-terminal", "gnome-terminal", "--", "bash", script_path.c_str(), nullptr);
                            execlp("x-terminal-emulator", "x-terminal-emulator", "-e", "bash", script_path.c_str(),
                                   nullptr);
                            execlp("xterm", "xterm", "-e", "bash", script_path.c_str(), nullptr);
#endif
                            _exit(1);
                        }
                        _exit(0); // intermediate child exits immediately
                    }
                    else if (pid > 0)
                    {
                        waitpid(pid, nullptr, 0); // reap intermediate child (instant)
                    }
#endif
                });

            content_sizer->Add(order_label, 0, wxEXPAND | wxALL, em);
#ifdef _WIN32
            content_sizer->Add(order_listbox, 0, wxEXPAND | wxLEFT | wxRIGHT, em);
#else
            content_sizer->Add(order_border, 0, wxEXPAND | wxLEFT | wxRIGHT, em);
#endif
            content_sizer->Add(order_btn_sizer, 0, wxEXPAND | wxALL, em);
            content_sizer->AddSpacer(em * 2);

            content_sizer->Add(console_label, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, em);
            content_sizer->Add(console_desc, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, em);
            content_sizer->Add(btn_console, 0, wxLEFT | wxRIGHT | wxTOP, em);
            content_sizer->AddSpacer(em * 2);

            content_sizer->Add(reset_btn, 0, wxLEFT | wxRIGHT | wxBOTTOM, em);

            scrolled->SetSizer(content_sizer);

            // Outer sizer: scrolled window must be item 0 (constructor post-build loop expects this)
            wxBoxSizer *tab_sizer = new wxBoxSizer(wxVERTICAL);
            tab_sizer->Add(scrolled, 1, wxEXPAND);
            tab_sizer->SetSizeHints(pp_tab);
            pp_tab->SetSizer(tab_sizer);
        }

#if ENABLE_ENVIRONMENT_MAP
        // Add "Render" tab
        m_optgroup_render = create_options_tab(L("Render"), tabs);
        m_optgroup_render->on_change = [this](t_config_option_key opt_key, boost::any value)
        {
            if (auto it = m_values.find(opt_key); it != m_values.end())
            {
                m_values.erase(
                    it); // we shouldn't change value, if some of those parameters were selected, and then deselected
                return;
            }
            m_values[opt_key] = boost::any_cast<bool>(value) ? "1" : "0";
        };

        append_bool_option(m_optgroup_render, "use_environment_map", L("Use environment map"),
                           L("If enabled, renders object using the environment map."),
                           app_config->get_bool("use_environment_map"));

        activate_options_tab(m_optgroup_render);
#endif // ENABLE_ENVIRONMENT_MAP
    }

    // update alignment of the controls for all tabs
    update_ctrls_alignment();

    auto sizer = new wxBoxSizer(wxVERTICAL);
    int em = wxGetApp().em_unit();
    sizer->Add(tabs, 1, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, em / 2);

    auto buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    wxGetApp().SetWindowVariantForButton(buttons->GetAffirmativeButton());
    wxGetApp().SetWindowVariantForButton(buttons->GetCancelButton());
    this->Bind(wxEVT_BUTTON, &PreferencesDialog::accept, this, wxID_OK);
    this->Bind(wxEVT_BUTTON, &PreferencesDialog::revert, this, wxID_CANCEL);

    for (int id : {wxID_OK, wxID_CANCEL})
    {
        wxWindow *btn = FindWindowById(id, this);
#ifdef _WIN32
        if (btn && btn->GetHWND())
            wxGetApp().UpdateDarkUI(static_cast<wxButton *>(btn));
#endif
    }

    sizer->Add(buttons, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM | wxTOP, em);

    SetSizer(sizer);
    sizer->SetSizeHints(this);
    if (wxGetApp().is_gcode_viewer())
        this->CenterOnScreen();
    else
        this->CenterOnParent();
}

std::vector<ConfigOptionsGroup *> PreferencesDialog::optgroups()
{
    std::vector<ConfigOptionsGroup *> out;
    out.reserve(4);
    for (ConfigOptionsGroup *opt : {m_optgroup_general.get(), m_optgroup_camera.get(), m_optgroup_gui.get(),
                                    m_optgroup_other.get(), m_optgroup_cpu.get()
#if ENABLE_ENVIRONMENT_MAP
                                                                ,
                                    m_optgroup_render.get()
#endif // ENABLE_ENVIRONMENT_MAP
         })
        if (opt)
            out.emplace_back(opt);
    return out;
}

void PreferencesDialog::update_ctrls_alignment()
{
    int max_ctrl_width{0};
    for (ConfigOptionsGroup *og : this->optgroups())
        if (int max = og->custom_ctrl->get_max_win_width(); max_ctrl_width < max)
            max_ctrl_width = max;
    if (max_ctrl_width)
        for (ConfigOptionsGroup *og : this->optgroups())
            og->custom_ctrl->set_max_win_width(max_ctrl_width);
}

void PreferencesDialog::accept(wxEvent &)
{
    // if(wxGetApp().is_editor()) {
    // 	if (const auto it = m_values.find("downloader_url_registered"); it != m_values.end())
    // 		downloader->allow(it->second == "1");
    // 	if (!downloader->on_finish())
    // 		return;
    // #if defined(__linux__) && defined(SLIC3R_DESKTOP_INTEGRATION)
    // 	if(DownloaderUtils::Worker::perform_registration_linux)
    // 		DesktopIntegrationDialog::perform_downloader_desktop_integration();
    // #endif //(__linux__) && defined(SLIC3R_DESKTOP_INTEGRATION)
    // }

    // preFlight: capture the selected theme directly from the field rather than relying on the
    // combo's on_change, then treat a change like the other recreate-on-restart options.
    if (m_optgroup_gui)
    {
        if (Field *theme_field = m_optgroup_gui->get_field("theme"))
        {
            std::string sel;
            const boost::any &v = theme_field->get_value();
            if (v.type() == typeid(std::string))
                sel = boost::any_cast<std::string>(v);
            else if (v.type() == typeid(wxString))
                sel = into_u8(boost::any_cast<wxString>(v));
            if (!sel.empty() && sel != current_theme_selection())
                m_values["theme"] = sel;
        }
    }

    std::vector<std::string> options_to_recreate_GUI = {"no_defaults", "theme", "font_pt_size",
                                                        "suppress_round_corners", "legacy_prepare_layout"};

    for (const std::string &option : options_to_recreate_GUI)
    {
        if (m_values.find(option) != m_values.end())
        {
            wxString title = wxGetApp().is_editor() ? wxString(SLIC3R_APP_NAME) : wxString(GCODEVIEWER_APP_NAME);
            title += " - " + _L("Changes for the critical options");
            MessageDialog dialog(nullptr,
                                 _L("Changing some options will trigger application restart.\n"
                                    "You will lose the content of the plater.") +
                                     "\n\n" + _L("Do you want to proceed?"),
                                 title, wxICON_QUESTION | wxYES | wxNO);
            if (dialog.ShowModal() == wxID_YES)
            {
                m_recreate_GUI = true;
            }
            else
            {
                for (const std::string &option : options_to_recreate_GUI)
                    m_values.erase(option);
            }
            break;
        }
    }

    auto app_config = get_app_config();

    for (const std::string &key : {"old_settings_layout_mode", "dlg_settings_layout_mode"})
    {
        auto it = m_values.find(key);
        if (it != m_values.end() && app_config->get(key) != it->second)
        {
            m_settings_layout_changed = true;
            break;
        }
    }

#if 0 //#ifdef _WIN32 // #ysDarkMSW - Allow it when we deside to support the sustem colors for application
	if (m_values.find("always_dark_color_mode") != m_values.end())
		wxGetApp().force_sys_colors_update();
#endif

    for (std::map<std::string, std::string>::iterator it = m_values.begin(); it != m_values.end(); ++it)
        app_config->set(it->first, it->second);

    // CPU tab writes through to AppConfig from its own on_change handler, so nothing to commit here.

    // Label colors and mode palette are hardcoded

    EndModal(wxID_OK);

    wxGetApp().update_ui_from_settings();
    clear_cache();
}

void PreferencesDialog::revert(wxEvent &)
{
    auto app_config = get_app_config();

    if (m_custom_toolbar_size != atoi(app_config->get("custom_toolbar_size").c_str()))
    {
        app_config->set("custom_toolbar_size", (boost::format("%d") % m_custom_toolbar_size).str());
        m_icon_size_slider->SetValue(m_custom_toolbar_size);
    }
    if (m_use_custom_toolbar_size != (get_app_config()->get_bool("use_custom_toolbar_size")))
    {
        app_config->set("use_custom_toolbar_size", m_use_custom_toolbar_size ? "1" : "0");

        m_optgroup_gui->set_value("use_custom_toolbar_size", m_use_custom_toolbar_size);
        m_icon_size_sizer->ShowItems(m_use_custom_toolbar_size);
        refresh_og(m_optgroup_gui);
    }

    for (auto value : m_values)
    {
        const std::string &key = value.first;

        if (key == "default_action_on_dirty_project")
        {
            m_optgroup_general->set_value(key, app_config->get(key).empty());
            continue;
        }
        if (key == "default_action_on_close_application" || key == "default_action_on_select_preset" ||
            key == "default_action_on_new_project")
        {
            m_optgroup_general->set_value(key, app_config->get(key) == "none");
            continue;
        }
        if (key == "notify_release")
        {
            m_optgroup_gui->set_value(key, s_keys_map_NotifyReleaseMode.at(app_config->get(key)));
            continue;
        }
        if (key == "old_settings_layout_mode")
        {
            m_rb_old_settings_layout_mode->SetValue(app_config->get_bool(key));
            m_settings_layout_changed = false;
            continue;
        }
        if (key == "dlg_settings_layout_mode")
        {
            m_rb_dlg_settings_layout_mode->SetValue(app_config->get_bool(key));
            m_settings_layout_changed = false;
            continue;
        }

        for (auto opt_group : {m_optgroup_general, m_optgroup_camera, m_optgroup_gui, m_optgroup_other
#if ENABLE_ENVIRONMENT_MAP
                               ,
                               m_optgroup_render
#endif // ENABLE_ENVIRONMENT_MAP
             })
        {
            if (opt_group->set_value(key, app_config->get_bool(key)))
                break;
        }
    }

    clear_cache();
    EndModal(wxID_CANCEL);
}

void PreferencesDialog::msw_rescale()
{
    for (ConfigOptionsGroup *og : this->optgroups())
        og->msw_rescale();

    update_ctrls_alignment();

    msw_buttons_rescale(this, em_unit(), {wxID_OK, wxID_CANCEL});

    layout();
}

void PreferencesDialog::on_sys_color_changed()
{
#ifdef _WIN32
    wxGetApp().UpdateDlgDarkUI(this);
#endif
}

void PreferencesDialog::layout()
{
    const int em = em_unit();

    SetMinSize(wxSize(47 * em, 28 * em));
    Fit();

    Refresh();
}

void PreferencesDialog::clear_cache()
{
    m_values.clear();
    m_custom_toolbar_size = -1;
}

void PreferencesDialog::refresh_og(std::shared_ptr<ConfigOptionsGroup> og)
{
    og->parent()->Layout();
    tabs->Layout();
    //	this->layout();
}

void PreferencesDialog::create_icon_size_slider()
{
    const auto app_config = get_app_config();

    const int em = em_unit();

    m_icon_size_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxWindow *parent = m_optgroup_gui->parent();
    wxGetApp().UpdateDarkUI(parent);

    if (isOSX)
        // For correct rendering of the slider and value label under OSX
        // we should use system default background
        parent->SetBackgroundStyle(wxBG_STYLE_ERASE);

    auto label = new wxStaticText(parent, wxID_ANY, _L("Icon size in a respect to the default size") + " (%) :");

    m_icon_size_sizer->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | (isOSX ? 0 : wxLEFT), em);

    const int def_val = atoi(app_config->get("custom_toolbar_size").c_str());

    long style = wxSL_HORIZONTAL;
    if (!isOSX)
        style |= wxSL_LABELS | wxSL_AUTOTICKS;

    m_icon_size_slider = new wxSlider(parent, wxID_ANY, def_val, 30, 100, wxDefaultPosition, wxDefaultSize, style);

    m_icon_size_slider->SetTickFreq(10);
    m_icon_size_slider->SetPageSize(10);
    m_icon_size_slider->SetToolTip(_L("Select toolbar icon size in respect to the default one."));

    m_icon_size_sizer->Add(m_icon_size_slider, 1, wxEXPAND);

    wxStaticText *val_label{nullptr};
    if (isOSX)
    {
        val_label = new wxStaticText(parent, wxID_ANY, wxString::Format("%d", def_val));
        m_icon_size_sizer->Add(val_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, em);
    }

    m_icon_size_slider->Bind(wxEVT_SLIDER,
                             (
                                 [this, val_label, app_config](wxCommandEvent e)
                                 {
                                     auto val = m_icon_size_slider->GetValue();

                                     app_config->set("custom_toolbar_size", (boost::format("%d") % val).str());
                                     wxGetApp().plater()->get_current_canvas3D()->render();

                                     if (val_label)
                                         val_label->SetLabelText(wxString::Format("%d", val));
                                 }),
                             m_icon_size_slider->GetId());

    for (wxWindow *win : std::vector<wxWindow *>{m_icon_size_slider, label, val_label})
    {
        if (!win)
            continue;
        win->SetFont(wxGetApp().normal_font());

        if (isOSX)
            continue; // under OSX we use wxBG_STYLE_ERASE
        win->SetBackgroundStyle(wxBG_STYLE_PAINT);
    }

    m_optgroup_gui->sizer->Add(m_icon_size_sizer, 0, wxEXPAND | wxALL, em);
}

void PreferencesDialog::create_settings_mode_widget()
{
    wxWindow *parent = m_optgroup_gui->parent();

    wxString title = L("Layout Options");
    wxStaticBox *stb = new wxStaticBox(parent, wxID_ANY, _(title));
    wxGetApp().UpdateDarkUI(stb);
    if (!wxOSX)
        stb->SetBackgroundStyle(wxBG_STYLE_PAINT);
    stb->SetFont(wxGetApp().normal_font());

    wxSizer *stb_sizer = new wxStaticBoxSizer(stb, wxVERTICAL);

    auto app_config = get_app_config();
    std::vector<wxString> choices = {_L("Old regular layout with the tab bar"), _L("Settings in non-modal window")};
    int id = -1;
    auto add_radio = [this, parent, stb_sizer, choices](wxRadioButton **rb, int id, bool select)
    {
        *rb = new wxRadioButton(parent, wxID_ANY, choices[id], wxDefaultPosition, wxDefaultSize,
                                id == 0 ? wxRB_GROUP : 0);
        stb_sizer->Add(*rb);
        (*rb)->SetValue(select);
        (*rb)->Bind(wxEVT_RADIOBUTTON,
                    [this, id](wxCommandEvent &)
                    {
                        m_values["old_settings_layout_mode"] = (id == 0) ? "1" : "0";
                        m_values["dlg_settings_layout_mode"] = (id == 1) ? "1" : "0";
                    });
    };

    add_radio(&m_rb_old_settings_layout_mode, ++id, app_config->get_bool("old_settings_layout_mode"));
    add_radio(&m_rb_dlg_settings_layout_mode, ++id, app_config->get_bool("dlg_settings_layout_mode"));

    std::string opt_key = "settings_layout_mode";
    m_blinkers[opt_key] = new BlinkingBitmap(parent);

    auto sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(m_blinkers[opt_key], 0, wxRIGHT, wxGetApp().em_unit() / 5);
    sizer->Add(stb_sizer, 1, wxALIGN_CENTER_VERTICAL);
    m_optgroup_gui->sizer->Add(sizer, 0, wxEXPAND | wxTOP, em_unit());

    append_preferences_option_to_searcher(m_optgroup_gui, opt_key, title);
}

void PreferencesDialog::create_settings_text_color_widget()
{
    wxWindow *parent = m_optgroup_gui->parent();

    wxString title = L("Text colors");
    wxStaticBox *stb = new wxStaticBox(parent, wxID_ANY, _(title));
    wxGetApp().UpdateDarkUI(stb);
    if (!wxOSX)
        stb->SetBackgroundStyle(wxBG_STYLE_PAINT);

    std::string opt_key = "text_colors";
    m_blinkers[opt_key] = new BlinkingBitmap(parent);

    wxSizer *stb_sizer = new wxStaticBoxSizer(stb, wxVERTICAL);
    GUI_Descriptions::FillSizerWithTextColorDescriptions(stb_sizer, parent, &m_sys_colour, &m_mod_colour);

    auto sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(m_blinkers[opt_key], 0, wxRIGHT, wxGetApp().em_unit() / 5);
    sizer->Add(stb_sizer, 1, wxALIGN_CENTER_VERTICAL);

    m_optgroup_gui->sizer->Add(sizer, 0, wxEXPAND | wxTOP, em_unit());

    append_preferences_option_to_searcher(m_optgroup_gui, opt_key, title);
}

void PreferencesDialog::create_settings_mode_color_widget()
{
    wxWindow *parent = m_optgroup_gui->parent();

    wxString title = L("Mode markers");
    wxStaticBox *stb = new wxStaticBox(parent, wxID_ANY, _(title));
    wxGetApp().UpdateDarkUI(stb);
    if (!wxOSX)
        stb->SetBackgroundStyle(wxBG_STYLE_PAINT);

    std::string opt_key = "mode_markers";
    m_blinkers[opt_key] = new BlinkingBitmap(parent);

    wxSizer *stb_sizer = new wxStaticBoxSizer(stb, wxVERTICAL);

    // Mode color markers description
    m_mode_palette = wxGetApp().get_mode_palette();
    GUI_Descriptions::FillSizerWithModeColorDescriptions(stb_sizer, parent,
                                                         {&m_mode_simple, &m_mode_advanced, &m_mode_expert},
                                                         m_mode_palette);

    auto sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(m_blinkers[opt_key], 0, wxRIGHT, wxGetApp().em_unit() / 5);
    sizer->Add(stb_sizer, 1, wxALIGN_CENTER_VERTICAL);

    m_optgroup_gui->sizer->Add(sizer, 0, wxEXPAND | wxTOP, em_unit());

    append_preferences_option_to_searcher(m_optgroup_gui, opt_key, title);
}

void PreferencesDialog::create_settings_font_widget()
{
    wxWindow *parent = m_optgroup_other->parent();
    wxGetApp().UpdateDarkUI(parent);

    const wxString title = L("Application font size");
    wxStaticBox *stb = new wxStaticBox(parent, wxID_ANY, _(title));
    if (!wxOSX)
        stb->SetBackgroundStyle(wxBG_STYLE_PAINT);

    const std::string opt_key = "font_pt_size";
    m_blinkers[opt_key] = new BlinkingBitmap(parent);

    wxSizer *stb_sizer = new wxStaticBoxSizer(stb, wxHORIZONTAL);

    wxStaticText *font_example = new wxStaticText(parent, wxID_ANY, "Application text");
    int val = wxGetApp().normal_font().GetPointSize();
    SpinInput *size_sc = new SpinInput(parent, format_wxstr("%1%", val), "", wxDefaultPosition,
                                       wxSize(15 * em_unit(), -1),
                                       wxTE_PROCESS_ENTER | wxSP_ARROW_KEYS
#ifdef _WIN32
                                           | wxBORDER_SIMPLE
#endif
                                       ,
                                       8, wxGetApp().get_max_font_pt_size());
    wxGetApp().UpdateDarkUI(size_sc);

    auto apply_font = [this, font_example, opt_key, stb_sizer](const int val, const wxFont &font)
    {
        font_example->SetFont(font);
        m_values[opt_key] = format("%1%", val);
        stb_sizer->Layout();
#ifdef __linux__
        CallAfter([this]() { refresh_og(m_optgroup_other); });
#else
        refresh_og(m_optgroup_other);
#endif
    };

    auto change_value = [size_sc, apply_font](wxCommandEvent &evt)
    {
        const int val = size_sc->GetValue();
        wxFont font = wxGetApp().normal_font();
        font.SetPointSize(val);

        apply_font(val, font);
    };
    size_sc->Bind(wxEVT_SPINCTRL, change_value);
    size_sc->Bind(wxEVT_TEXT_ENTER, change_value);

    auto revert_btn = new ScalableButton(parent, wxID_ANY, "undo");
    revert_btn->SetToolTip(_L("Revert font to default"));
    revert_btn->Bind(wxEVT_BUTTON,
                     [size_sc, apply_font](wxEvent &event)
                     {
                         wxFont font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
                         const int val = font.GetPointSize();
                         size_sc->SetValue(val);
                         apply_font(val, font);
                     });
    parent->Bind(
        wxEVT_UPDATE_UI,
        [size_sc](wxUpdateUIEvent &evt)
        {
            const int def_size = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT).GetPointSize();
            evt.Enable(def_size != size_sc->GetValue());
        },
        revert_btn->GetId());

    stb_sizer->Add(new wxStaticText(parent, wxID_ANY, _L("Font size") + ":"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT,
                   em_unit());
    stb_sizer->Add(size_sc, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxLEFT, em_unit());
    stb_sizer->Add(revert_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em_unit());
    wxBoxSizer *font_sizer = new wxBoxSizer(wxVERTICAL);
    font_sizer->Add(font_example, 1, wxALIGN_CENTER_HORIZONTAL);
    stb_sizer->Add(font_sizer, 1, wxALIGN_CENTER_VERTICAL);

    auto sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(m_blinkers[opt_key], 0, wxRIGHT, wxGetApp().em_unit() / 5);
    sizer->Add(stb_sizer, 1, wxALIGN_CENTER_VERTICAL);

    m_optgroup_other->sizer->Add(sizer, 1, wxEXPAND | wxTOP, em_unit());

    append_preferences_option_to_searcher(m_optgroup_other, opt_key, title);
}

void PreferencesDialog::create_downloader_path_sizer()
{
    wxWindow *parent = m_optgroup_other->parent();

    wxString title = L("Download path");
    std::string opt_key = "url_downloader_dest";
    m_blinkers[opt_key] = new BlinkingBitmap(parent);

    downloader = new DownloaderUtils::Worker(parent);

    auto sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(m_blinkers[opt_key], 0, wxRIGHT, wxGetApp().em_unit() / 5);
    sizer->Add(downloader, 1, wxALIGN_CENTER_VERTICAL);

    m_optgroup_other->sizer->Add(sizer, 0, wxEXPAND | wxTOP, em_unit());

    append_preferences_option_to_searcher(m_optgroup_other, opt_key, title);
}

void PreferencesDialog::init_highlighter(const t_config_option_key &opt_key)
{
    if (m_blinkers.find(opt_key) != m_blinkers.end())
        if (BlinkingBitmap *blinker = m_blinkers.at(opt_key); blinker)
        {
            m_highlighter.init(blinker);
            return;
        }

    for (auto opt_group : {m_optgroup_general, m_optgroup_camera, m_optgroup_gui, m_optgroup_other
#if ENABLE_ENVIRONMENT_MAP
                           ,
                           m_optgroup_render
#endif // ENABLE_ENVIRONMENT_MAP
         })
    {
        std::pair<OG_CustomCtrl *, bool *> ctrl = opt_group->get_custom_ctrl_with_blinking_ptr(opt_key, -1);
        if (ctrl.first && ctrl.second)
        {
            m_highlighter.init(ctrl);
            break;
        }
    }
}

} // namespace GUI
} // namespace Slic3r
