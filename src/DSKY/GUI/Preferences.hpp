///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2018 - 2023 Oleksandra Iushchenko @YuSanka, David Kocík @kocikdav, Vojtěch Bubník @bubnikv, Enrico Turri @enricoturri1966
///|/ Copyright (c) 2021 Jurriaan Pruis
///|/
///|/ Copyright (c) Prusa Research 2016 - 2018 Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2013 - 2014 Alessandro Ranellucci @alranel
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include "GUI.hpp"
#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"

#include <wx/dialog.h>
#include <wx/timer.h>
#include <vector>
#include <map>

class wxColourPickerCtrl;
class wxBookCtrlBase;
class wxSlider;
class wxRadioButton;

namespace Luminary
{
// Enum backing the "Maximum slicing threads" dropdown in Preferences > CPU. Keys stored in AppConfig
// are numeric strings ("0", "8", ...), so startup code can still read the value with atoi without
// knowing about the enum. "0" / Auto means "no cap; use all available cores".
enum CpuMaxThreadsMode
{
    CpuMaxThreadsAuto,
    CpuMaxThreadsT1,
    CpuMaxThreadsT2,
    CpuMaxThreadsT4,
    CpuMaxThreadsT6,
    CpuMaxThreadsT8,
    CpuMaxThreadsT12,
    CpuMaxThreadsT16,
    CpuMaxThreadsT24,
    CpuMaxThreadsT32,
    CpuMaxThreadsCount,
};

enum NotifyReleaseMode
{
    NotifyReleaseAll,
    NotifyReleaseOnly,
    NotifyReleaseNone
};

enum CanvasLightingQuality
{
    CanvasLightingAuto,
    CanvasLightingBasic,
    CanvasLightingEnhanced,
    CanvasLightingFull,
    CanvasLightingCount,
};

enum CanvasSsaaMode
{
    CanvasSsaaOff,
    CanvasSsaa15x,
    CanvasSsaa20x,
    CanvasSsaaCount,
};

enum CanvasMouseScheme
{
    CanvasMouseSchemeDefault,
    CanvasMouseSchemeBlender,
    CanvasMouseSchemeFusion,
    CanvasMouseSchemeSolidWorks,
    CanvasMouseSchemeTinkercad,
    CanvasMouseSchemeCount,
};

enum CanvasMsaaMode
{
    CanvasMsaaAuto,
    CanvasMsaaOff,
    CanvasMsaa2x,
    CanvasMsaa4x,
    CanvasMsaa8x,
    CanvasMsaa16x,
    CanvasMsaaCount,
};

enum PreviewDetailLevel
{
    PreviewDetail1M,
    PreviewDetail5M,
    PreviewDetail10M,
    PreviewDetail20M,
    PreviewDetailFull,
    PreviewDetailCount,
};
} // namespace Luminary

namespace DSKY
{
using namespace Luminary;

class ConfigOptionsGroup;
class OG_CustomCtrl;

namespace DownloaderUtils
{
class Worker;
}

class PreferencesDialog : public DPIDialog
{
    std::map<std::string, std::string> m_values;
    // Pre-dialog values of restart-bound performance settings (they write through
    // to AppConfig on change, bypassing m_values).
    std::map<std::string, std::string> m_perf_restart_originals;
    std::shared_ptr<ConfigOptionsGroup> m_optgroup_general;
    std::shared_ptr<ConfigOptionsGroup> m_optgroup_camera;
    std::shared_ptr<ConfigOptionsGroup> m_optgroup_gui;
    std::shared_ptr<ConfigOptionsGroup> m_optgroup_other;
    std::shared_ptr<ConfigOptionsGroup> m_optgroup_preprocessing;
    std::shared_ptr<ConfigOptionsGroup> m_optgroup_cpu;
#if ENABLE_ENVIRONMENT_MAP
    std::shared_ptr<ConfigOptionsGroup> m_optgroup_render;
#endif // ENABLE_ENVIRONMENT_MAP
    wxSizer *m_icon_size_sizer{nullptr};
    wxSlider *m_icon_size_slider{nullptr};
    wxRadioButton *m_rb_old_settings_layout_mode{nullptr};
    wxRadioButton *m_rb_dlg_settings_layout_mode{nullptr};

    wxColourPickerCtrl *m_sys_colour{nullptr};
    wxColourPickerCtrl *m_mod_colour{nullptr};

    std::vector<wxColour> m_mode_palette;
    wxColourPickerCtrl *m_mode_simple{nullptr};
    wxColourPickerCtrl *m_mode_advanced{nullptr};
    wxColourPickerCtrl *m_mode_expert{nullptr};

    DownloaderUtils::Worker *downloader{nullptr};

    wxBookCtrlBase *tabs{nullptr};

    bool isOSX{false};
    bool m_settings_layout_changed{false};
    bool m_recreate_GUI{false};

    int m_custom_toolbar_size{-1};
    bool m_use_custom_toolbar_size{false};

public:
    explicit PreferencesDialog(wxWindow *paren);
    ~PreferencesDialog() = default;

    bool settings_layout_changed() const { return m_settings_layout_changed; }
    bool recreate_GUI() const { return m_recreate_GUI; }
    void build();
    void update_ctrls_alignment();
    void accept(wxEvent &);
    void revert(wxEvent &);
    void show(const std::string &highlight_option = std::string(), const std::string &tab_name = std::string());

protected:
    void msw_rescale();
    void on_dpi_changed(const wxRect &suggested_rect) override { msw_rescale(); }
    void on_sys_color_changed() override;
    void layout();
    void clear_cache();
    void refresh_og(std::shared_ptr<ConfigOptionsGroup> og);
    void refresh_og(ConfigOptionsGroup *og);
    void create_icon_size_slider();
    void create_settings_mode_widget();
    void create_settings_text_color_widget();
    void create_settings_mode_color_widget();
    void create_settings_font_widget();
    void create_downloader_path_sizer();
    void init_highlighter(const t_config_option_key &opt_key);
    std::vector<ConfigOptionsGroup *> optgroups();

    HighlighterForWx m_highlighter;
    std::map<std::string, BlinkingBitmap *> m_blinkers;
};

} // namespace DSKY

