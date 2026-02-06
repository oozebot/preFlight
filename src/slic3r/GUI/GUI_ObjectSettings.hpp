///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2018 - 2022 Enrico Turri @enricoturri1966, Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena
///|/ Copyright (c) 2019 Maeyanie @Maeyanie
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef slic3r_GUI_ObjectSettings_hpp_
#define slic3r_GUI_ObjectSettings_hpp_

#include <memory>
#include <vector>
#include <wx/panel.h>
#include "wxExtensions.hpp"

class wxBoxSizer;
class ScrollablePanel;

namespace Slic3r
{
class DynamicPrintConfig;
class ModelConfig;
namespace GUI
{
class ConfigOptionsGroup;

class OG_Settings
{
protected:
    std::shared_ptr<ConfigOptionsGroup> m_og;
    wxWindow *m_parent;

public:
    OG_Settings(wxWindow *parent, const bool staticbox);
    virtual ~OG_Settings() {}

    virtual bool IsShown();
    virtual void Show(const bool show);
    virtual void Hide();
    virtual void UpdateAndShow(const bool show);

    virtual wxSizer *get_sizer();
    ConfigOptionsGroup *get_og() { return m_og.get(); }
    const ConfigOptionsGroup *get_og() const { return m_og.get(); }
    wxWindow *parent() const { return m_parent; }
};

class ObjectSettings : public OG_Settings
{
    // Scrollable panel for settings list (shows scrollbar when content exceeds max height)
    ScrollablePanel *m_scroll_panel{nullptr};
    // sizer for extra Object/Part's settings
    wxBoxSizer *m_settings_list_sizer{nullptr};
    // option groups for settings
    std::vector<std::shared_ptr<ConfigOptionsGroup>> m_og_settings;

    ScalableBitmap m_bmp_delete;
    ScalableBitmap m_bmp_delete_focus;

public:
    ObjectSettings(wxWindow *parent);
    ~ObjectSettings() {}

    bool update_settings_list();
    /* Additional check for override options: Add options, if its needed.
     * Example: if Infill is set to 100%, and Fill Pattern is missed in config_to,
     * we should add fill_pattern to avoid endless loop in update
     */
    bool add_missed_options(ModelConfig *config_to, const DynamicPrintConfig &config_from);
    void update_config_values(ModelConfig *config);
    void UpdateAndShow(const bool show) override;
    void sys_color_changed();
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_ObjectSettings_hpp_
